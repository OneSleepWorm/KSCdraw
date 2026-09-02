/**
 * @file    tim_clock.c
 * @note    定时器时钟应用 — 统一 TIM1/2/3/4 语义 (Linux BSP, pthread 模拟)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  tim_clock
 * dep:     NULL
 * 平台:    Linux (__USE_PC__ + __USE_LINUX__)
 *
 * ============================================================
 * 使用方法 (与 STM32 / PC 完全一致)
 * ============================================================
 *   app_t* t = appget("tim_clock");
 *   t->user_func = my_cb;
 *   t->input_data = my_ud;
 *   appopen(t);
 *   appwrite(t, NULL, 250, 0x41);   // TIM1, 设置周期
 *   appwrite(t, NULL, 1,   0x42);   // TIM1, 启动
 *   appclose(t);
 *
 * mode = (inst<<4) | op,  inst=1..4
 *   op=0: NOP
 *   op=1: 设置周期 count (ms)
 *   op=2: count>0 启动, count=0 停止
 *
 * ============================================================
 * 与 bsp/pc/tim_clock.c 的差异
 * ============================================================
 *   - HANDLE / CreateThread       → pthread_t / pthread_create
 *   - WaitForSingleObject + CloseHandle → pthread_join
 *   - GetTickCount()              → clock_gettime(CLOCK_MONOTONIC)
 *   - Sleep(ms)                   → nanosleep()
 *
 * 线程语义与 PC 版一致: 回调 ctx->cb[i]() 在定时器线程上下文中执行
 * (STM32 上为中断上下文), 调用方需自行保证非重入安全。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

typedef struct {
    void_func_t cb[4];
    void*       ud[4];
    uint32_t    period[4];
    volatile uint8_t running[4];
    pthread_t   thread;
    volatile uint8_t thread_run;
    uint32_t    counter[4];
} tim_ctx_t;

static uint32_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

static void* tim_linux_thread(void* param)
{
    tim_ctx_t* ctx = (tim_ctx_t*)param;
    while (ctx->thread_run) {
        uint32_t t0 = now_ms();
        uint32_t sleep_ms_val = 1000;
        for (int i = 0; i < 4; i++) {
            if (ctx->running[i] && ctx->period[i] > 0) {
                uint32_t rem = ctx->period[i] - ctx->counter[i];
                if (rem < sleep_ms_val) sleep_ms_val = rem;
            }
        }
        if (sleep_ms_val < 1) sleep_ms_val = 1;
        sleep_ms(sleep_ms_val);

        uint32_t elapsed = now_ms() - t0;
        if (elapsed == 0) elapsed = 1;   /* 单调时钟保证不自减 */
        for (int i = 0; i < 4; i++) {
            if (ctx->running[i] && ctx->period[i] > 0) {
                ctx->counter[i] += elapsed;
                while (ctx->running[i] && ctx->counter[i] >= ctx->period[i]) {
                    ctx->counter[i] -= ctx->period[i];
                    if (ctx->cb[i])
                        ctx->cb[i](ctx->ud[i]);
                }
            }
        }
    }
    return NULL;
}

/* 停止并回收线程; 无线程在跑则直接返回 */
static void tim_thread_stop(tim_ctx_t* ctx)
{
    if (!ctx->thread_run) return;
    ctx->thread_run = 0;
    /* 线程每次最多睡 1000ms, join 必然在 ~1s 内返回 */
    pthread_join(ctx->thread, NULL);
}

static int tim_thread_start(tim_ctx_t* ctx)
{
    if (ctx->thread_run) return 0;
    ctx->thread_run = 1;
    if (pthread_create(&ctx->thread, NULL, tim_linux_thread, ctx) != 0) {
        ctx->thread_run = 0;
        return -1;
    }
    return 0;
}

static int tim_app_open(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)osmalloc(sizeof(tim_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(tim_ctx_t));
    app->app_data = ctx;
    return 0;
}

static int tim_app_close(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;
    tim_thread_stop(ctx);
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

static int tim_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)data;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;

    uint32_t inst = mode >> 4;
    uint32_t op   = mode & 0x0F;
    if (inst < 1 || inst > 4) return -1;
    int i = (int)(inst - 1);

    switch (op) {
    case 0:
        return 1;

    case 1:
        ctx->period[i] = count;
        ctx->counter[i] = 0;
        return 1;

    case 2:
        if (count) {
            ctx->cb[i] = app->user_func;
            ctx->ud[i] = app->input_data;
            ctx->running[i] = 1;
            ctx->counter[i] = 0;
            if (tim_thread_start(ctx) != 0) {
                ctx->running[i] = 0;
                return -1;
            }
        } else {
            ctx->running[i] = 0;
            int any = 0;
            for (int j = 0; j < 4; j++)
                if (ctx->running[j]) { any = 1; break; }
            if (!any) tim_thread_stop(ctx);
        }
        return 1;

    default:
        return -1;
    }
}

static int tim_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)count;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx || !data) return -1;

    uint32_t inst = mode;
    if (inst < 1 || inst > 4) return -1;

    *(uint32_t*)data = ctx->period[inst - 1];
    return 4;
}

static int cmd_regcb(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    int i = (int)(inst - 1);
    ctx->cb[i] = app->user_func;
    ctx->ud[i] = app->input_data;
    ctx->running[i] = 1;
    return tim_thread_start(ctx) == 0 ? 1 : -1;
}

static int cmd_period(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i') || !APPCMD_HAS(argv, 't')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    uint32_t ms   = strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    int i = (int)(inst - 1);
    ctx->period[i] = ms;
    ctx->counter[i] = 0;
    return 1;
}

static int cmd_start(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    int i = (int)(inst - 1);
    ctx->cb[i] = app->user_func;
    ctx->ud[i] = app->input_data;
    ctx->running[i] = 1;
    ctx->counter[i] = 0;
    return tim_thread_start(ctx) == 0 ? 1 : -1;
}

static int cmd_stop(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    int i = (int)(inst - 1);
    ctx->running[i] = 0;
    int any = 0;
    for (int j = 0; j < 4; j++)
        if (ctx->running[j]) { any = 1; break; }
    if (!any) tim_thread_stop(ctx);
    return 1;
}

static int cmd_timrd(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    if (app->output_data)
        *(uint32_t*)app->output_data = ctx->period[inst - 1];
    return 1;
}

/* ========================================================================
 *  cmd 派发表 + REGISTER_APP
 * ======================================================================== */
typedef int (*tim_cmd_h)(app_t*, const char**);
typedef struct { const char* name; tim_cmd_h handler; } tim_cmd_t;

static const tim_cmd_t tim_cmds[] = {
    {"regcb",  cmd_regcb},
    {"period", cmd_period},
    {"start",  cmd_start},
    {"stop",   cmd_stop},
    {"rd",     cmd_timrd},
    {NULL, NULL}
};

static int tim_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const tim_cmd_t* e = tim_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t tim_clock_ops = {
    .open  = tim_app_open,
    .close = tim_app_close,
    .read  = tim_app_read,
    .write = tim_app_write,
    .cmd   = tim_app_cmd,
};

REGISTER_APP("tim_clock", "0", &tim_clock_ops,
    "TIM1-4 clock app (Linux: pthread)");
