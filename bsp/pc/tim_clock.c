/**
 * @file    tim_clock.c
 * @note    定时器时钟应用 — 统一 TIM1/2/3/4 语义 (PC BSP, 后台线程模拟)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  tim_clock
 * dep:     NULL
 * 平台:    PC (__USE_PC__)
 *
 * ============================================================
 * 使用方法
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
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>
#include <windows.h>

typedef struct {
    void_func_t cb[4];
    void*       ud[4];
    uint32_t    period[4];
    volatile uint8_t running[4];
    HANDLE      thread;
    volatile uint8_t thread_run;
    uint32_t    counter[4];
} tim_ctx_t;

static DWORD WINAPI tim_pc_thread(LPVOID param)
{
    tim_ctx_t* ctx = (tim_ctx_t*)param;
    while (ctx->thread_run) {
        DWORD t0 = GetTickCount();
        DWORD sleep_ms = 1000;
        for (int i = 0; i < 4; i++) {
            if (ctx->running[i] && ctx->period[i] > 0) {
                uint32_t rem = ctx->period[i] - ctx->counter[i];
                if (rem < sleep_ms) sleep_ms = rem;
            }
        }
        if (sleep_ms < 1) sleep_ms = 1;
        Sleep(sleep_ms);

        DWORD elapsed = GetTickCount() - t0;
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
    return 0;
}

static int tim_app_open(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)osmalloc(sizeof(tim_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(tim_ctx_t));
    ctx->thread = NULL;
    ctx->thread_run = 0;
    app->app_data = ctx;
    return 0;
}

static int tim_app_close(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;
    ctx->thread_run = 0;
    if (ctx->thread) {
        WaitForSingleObject(ctx->thread, 500);
        CloseHandle(ctx->thread);
        ctx->thread = NULL;
    }
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
            if (!ctx->thread_run) {
                ctx->thread_run = 1;
                ctx->thread = CreateThread(NULL, 0, tim_pc_thread,
                                           ctx, 0, NULL);
                if (!ctx->thread) {
                    ctx->thread_run = 0;
                    ctx->running[i] = 0;
                    return -1;
                }
            }
        } else {
            ctx->running[i] = 0;
            int any = 0;
            for (int j = 0; j < 4; j++)
                if (ctx->running[j]) { any = 1; break; }
            if (!any && ctx->thread_run) {
                ctx->thread_run = 0;
                if (ctx->thread) {
                    WaitForSingleObject(ctx->thread, 500);
                    CloseHandle(ctx->thread);
                    ctx->thread = NULL;
                }
            }
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
    if (!ctx->thread_run) {
        ctx->thread_run = 1;
        ctx->thread = CreateThread(NULL, 0, tim_pc_thread, ctx, 0, NULL);
    }
    return 1;
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
    if (!ctx->thread_run) {
        ctx->thread_run = 1;
        ctx->thread = CreateThread(NULL, 0, tim_pc_thread, ctx, 0, NULL);
    }
    return 1;
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
    if (!any && ctx->thread_run) {
        ctx->thread_run = 0;
        if (ctx->thread) {
            WaitForSingleObject(ctx->thread, 500);
            CloseHandle(ctx->thread);
            ctx->thread = NULL;
        }
    }
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
    "TIM1-4 clock app (PC: background thread)");
