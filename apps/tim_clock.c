/**
 * @file    tim_clock.c
 * @note    定时器时钟应用 — 统一 TIM1/2/3/4
 * @flash   ~1402B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  tim_clock
 * dep:     NULL
 * 平台:    STM32 + PC
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

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>

#if __USE_STM32__
#include "stm32f1xx.h"
#endif

typedef struct {
    void_func_t cb[4];
    void*       ud[4];
    uint32_t    period[4];
#if __USE_STM32__
    uint8_t     enabled;
    uint32_t    rd_val;
#endif
#if __USE_PC__
    volatile uint8_t running[4];
    HANDLE      thread;
    volatile uint8_t thread_run;
    uint32_t    counter[4];
#endif
} tim_ctx_t;

/* ========================================================================
 *  STM32 平台
 * ======================================================================== */
#if __USE_STM32__

static app_t* tim_owners[4];

static TIM_TypeDef* tim_reg(uint8_t inst)
{
    static TIM_TypeDef* const map[] = {TIM1, TIM2, TIM3, TIM4};
    if (inst < 1 || inst > 4) return NULL;
    return map[inst - 1];
}

static const IRQn_Type irq_map[] = {
    TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn,
};

static void rcc_enable(uint8_t inst)
{
    if (inst == 1)
        RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    else if (inst == 2)
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    else if (inst == 3)
        RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    else
        RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB2ENR;
}

static void inst_init(app_t* app, tim_ctx_t* ctx, uint8_t inst)
{
    uint8_t bit = (uint8_t)(1 << (inst - 1));
    if (ctx->enabled & bit) return;

    ctx->cb[inst - 1] = app->user_func;
    ctx->ud[inst - 1] = app->input_data;

    rcc_enable(inst);

    TIM_TypeDef* tim = tim_reg(inst);
    uint32_t period = app->input_data ? (uint32_t)(uintptr_t)app->input_data : 1000;
    uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
    uint32_t arr = 10 * period - 1;
    tim->PSC = (uint16_t)psc;
    tim->ARR = (uint16_t)arr;
    tim->EGR |= TIM_EGR_UG;
    tim->CNT = 0;
    tim->SR = ~TIM_SR_UIF;

    NVIC_SetPriority(irq_map[inst - 1], 0);
    NVIC_EnableIRQ(irq_map[inst - 1]);

    tim_owners[inst - 1] = app;
    ctx->enabled |= bit;
}

static int tim_app_open(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)osmalloc(sizeof(tim_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(tim_ctx_t));
    app->app_data = ctx;
    app->output_data = &ctx->rd_val;
    return 0;
}

static int tim_app_close(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;
    for (int i = 0; i < 4; i++) {
        if (ctx->enabled & (1 << i)) {
            TIM_TypeDef* tim = tim_reg((uint8_t)(i + 1));
            if (tim) {
                tim->CR1 &= ~TIM_CR1_CEN;
                tim->DIER &= ~TIM_DIER_UIE;
            }
            NVIC_DisableIRQ(irq_map[i]);
            tim_owners[i] = NULL;
        }
    }
    osfree(ctx);
    app->app_data = NULL;
    app->output_data = NULL;
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

    inst_init(app, ctx, (uint8_t)inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;

    TIM_TypeDef* tim = tim_reg((uint8_t)inst);

    switch (op) {
    case 0:
        return 1;

    case 1: {
        uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
        uint32_t arr = 10 * count - 1;
        tim->PSC = (uint16_t)psc;
        tim->ARR = (uint16_t)arr;
        tim->EGR |= TIM_EGR_UG;
        return 1;
    }

    case 2:
        if (count) {
            tim->CNT = 0;
            tim->SR = ~TIM_SR_UIF;
            tim->DIER |= TIM_DIER_UIE;
            tim->CR1 |= TIM_CR1_CEN;
        } else {
            tim->CR1 &= ~TIM_CR1_CEN;
            tim->DIER &= ~TIM_DIER_UIE;
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
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;

    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    *(uint32_t*)data = (uint32_t)((tim->ARR + 1) / 10);
    return 4;
}

static int cmd_regcb(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    inst_init(app, ctx, (uint8_t)inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    return 1;
}

static int cmd_period(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i') || !APPCMD_HAS(argv, 't')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    uint32_t ms   = strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
    uint32_t arr = 10 * ms - 1;
    tim->PSC = (uint16_t)psc;
    tim->ARR = (uint16_t)arr;
    tim->EGR |= TIM_EGR_UG;
    return 1;
}

static int cmd_start(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    tim->CNT = 0;
    tim->SR = ~TIM_SR_UIF;
    tim->DIER |= TIM_DIER_UIE;
    tim->CR1 |= TIM_CR1_CEN;
    return 1;
}

static int cmd_stop(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    tim->CR1 &= ~TIM_CR1_CEN;
    tim->DIER &= ~TIM_DIER_UIE;
    return 1;
}

static int cmd_timrd(app_t* app, const char** argv)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 4) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    if (app->output_data)
        *(uint32_t*)app->output_data = (tim->ARR + 1) / 10;
    return 1;
}

static void tim_irq_handler(int idx)
{
    app_t* app = tim_owners[idx];
    if (!app) return;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return;

    TIM_TypeDef* tim = tim_reg((uint8_t)(idx + 1));
    if (tim->SR & TIM_SR_UIF) {
        tim->SR = ~TIM_SR_UIF;
        if ((tim->CR1 & TIM_CR1_CEN) && ctx->cb[idx])
            ctx->cb[idx](ctx->ud[idx]);
    }
}

void TIM1_UP_IRQHandler(void) { tim_irq_handler(0); }
void TIM2_IRQHandler(void)    { tim_irq_handler(1); }
void TIM3_IRQHandler(void)    { tim_irq_handler(2); }
void TIM4_IRQHandler(void)    { tim_irq_handler(3); }

/* ========================================================================
 *  PC 平台
 * ======================================================================== */
#elif __USE_PC__

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

#endif  /* __USE_PC__ */

/* ========================================================================
 *  共享部分 — cmd 派发表 + REGISTER_APP
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
    "TIM1-4 clock app (instance via mode=(inst<<4)|op)");
