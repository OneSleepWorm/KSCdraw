/**
 * @file    system.c
 * @note    KSCOS 系统 app — Linux (时间/内存服务)
 *
 * 固定地址 app, 提供时间/内存/初始化等内核服务。
 * Linux 无芯片初始化 (clock_gettime / nanosleep 为 POSIX API)。
 *
 * 与 bsp/pc/system.c 的差异:
 *   - GetTickCount()  → clock_gettime(CLOCK_MONOTONIC), 以首次调用为 0 基准
 *   - Sleep(ms)       → nanosleep()
 *   - sys_idle() 中额外驱动 SDL 事件泵 (见下)
 *
 * ============================================================
 * SDL 事件泵接入点
 * ============================================================
 * SDL 窗口必须周期性 SDL_PollEvent 才不会假死/无法关闭。主泵位于
 * bsp/linux/uart_serial.c 的 appread() (主循环每轮都走到), 此处在
 * sys_idle() 中补一次, 保证 delay/idle 期间窗口仍然响应。
 * kscgui_sdl_pump() 由同属 Linux 预设的 gui_drv.c 提供, 未建窗时为空操作。
 *
 * 不改动三平台共享的 src/main.c / src/KSCOSsystem.c。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscsystem.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

/* gui_drv.c (Linux) 提供的 SDL 事件泵; 未建窗时内部直接返回 */
extern void kscgui_sdl_pump(void);

/* STM32 有 SysTick 递增; Linux 占位 (clock_gettime 为准) */
volatile uint32_t sys_tick_ms = 0;

/* CLOCK_MONOTONIC 起点, 首次取时间时惰性初始化 (语义对齐"开机毫秒计数") */
static uint32_t s_time_base_ms = 0;
static int      s_time_inited = 0;

void system_platform_init(ksc_system_data_t* data)
{
    data->clock = 0;
    data->tick_ms = 0;
}

/* ================================================================
 * 时间服务
 * ================================================================ */
static uint32_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    uint32_t ms = (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u);
    if (!s_time_inited) {
        s_time_base_ms = ms;
        s_time_inited = 1;
    }
    return ms - s_time_base_ms;
}

static uint32_t sys_gettime(void)
{
    return now_ms();
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    /* 被信号打断时继续睡完剩余时间 */
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

static void sys_delay(uint32_t ms)
{
    sleep_ms(ms);
}

static void sys_idle(void)
{
    /* 驱动 SDL 窗口事件 (窗口未创建时为空操作) */
    kscgui_sdl_pump();
    sleep_ms(1);
}

/* ================================================================
 * app 生命周期
 * ================================================================ */
static ksc_system_data_t sys_data;   /* 静态 app_data (固定区) */

int system_app_open(app_t* app)
{
    if (app->app_data) return 0;
    mempool_init();
    system_platform_init(&sys_data);
    app->app_data = &sys_data;
    return 0;
}

int system_app_close(app_t* app)
{
    (void)app;
    return -1;   /* 系统 app 禁止关闭 */
}

int system_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    ksc_system_data_t* d = (ksc_system_data_t*)app->app_data;
    if (!d || !data || count < sizeof(uint32_t)) return -1;
    switch (mode) {
    case 0:
        *(uint32_t*)data = sys_gettime();
        return 4;
    case 1:
        *(uint32_t*)data = d->clock;
        return 4;
    default:
        return -1;
    }
}

int system_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app;
    switch (mode) {
    case 0:  /* malloc */
        if (!data || count == 0) return -1;
        {
            void** pp = (void**)data;
            *pp = mempool_alloc(count);
            return *pp ? 1 : -1;
        }
    case 1:  /* free */
        if (!data) return -1;
        {
            void** pp = (void**)data;
            mempool_free(*pp);
            *pp = NULL;
        }
        return 1;
    case 2:  /* delay */
        if (!data || count < sizeof(uint32_t)) return -1;
        sys_delay(*(uint32_t*)data);
        return 1;
    case 3:  /* idle */
        sys_idle();
        return 1;
    default:
        return -1;
    }
}

int system_app_cmd(app_t* app, const char* cmdname, const char** argv)
{
    ksc_system_data_t* d = (ksc_system_data_t*)app->app_data;
    if (!d) return -1;

    if (strcmp(cmdname, SYS_CMD_TIME) == 0) {
        if (app->output_data) *(uint32_t*)app->output_data = sys_gettime();
        return 1;
    }
    if (strcmp(cmdname, SYS_CMD_DELAY) == 0) {
        if (!APPCMD_HAS(argv, 't')) return -1;
        sys_delay((uint32_t)strtoul(argv[APPCMD_ARG('t')], NULL, 0));
        return 1;
    }
    if (strcmp(cmdname, SYS_CMD_IDLE) == 0) {
        sys_idle();
        return 1;
    }
    if (strcmp(cmdname, SYS_CMD_MEM) == 0) {
        if (app->output_fn) {
            mempool_stat_t st;
            mempool_get_stat(&st);
            char b[64];
            int n = snprintf(b, sizeof(b), "pool fail=%lu used=%luB/%luB peak=%luB\r\n",
                             (unsigned long)st.alloc_fail,
                             (unsigned long)st.bytes_used,
                             (unsigned long)st.bytes_total,
                             (unsigned long)st.bytes_peak);
            app->output_fn(b, (uint32_t)n, app->output_ctx);
            for (int c = 0; c < MEMPOOL_CLASS_MAX; c++) {
                n = snprintf(b, sizeof(b), "  %4uB %2u/%2u peak=%2u a=%u f=%u%s\r\n",
                             (unsigned)st.cls[c].block_size,
                             (unsigned)st.cls[c].used,
                             (unsigned)st.cls[c].total,
                             (unsigned)st.cls[c].peak,
                             (unsigned)st.cls[c].alloc_cnt,
                             (unsigned)st.cls[c].free_cnt,
                             (st.cls[c].used >= st.cls[c].total) ? " FULL" : "");
                if (n > 0) app->output_fn(b, (uint32_t)n, app->output_ctx);
            }
        }
        return 1;
    }
    return -1;
}

static const papp_ops_t system_ops = {
    .open  = system_app_open,
    .close = system_app_close,
    .read  = system_app_read,
    .write = system_app_write,
    .cmd   = system_app_cmd,
};

REGISTER_APP("system", "0", &system_ops, "KSCOS kernel system service (clock/time/mem)");
