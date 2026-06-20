/**
 * @file    tim_clocktask.c
 * @note    定时器周期回调驱动 (PC / STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 驱动名:  tim_clocktask
 * ops_name: clock
 * 平台:    PC (__USE_PC__) / STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* tmr = bus_getdriver("tim", 2, "clock");
 *   if (!tmr) while(1);
 *   tmr->callback  = my_tick_callback;
 *   tmr->user_data = my_data;
 *   ddopen(tmr);                            // 初始化, 默认周期 1000ms, 不计数
 *   ddwrite(tmr, NULL, 500, 0);             // mode=0: 设周期 500ms
 *   ddwrite(tmr, NULL, 1,   1);             // mode=1, count=1: 启动
 *   // ... 定时器在后台运行 ...
 *   ddwrite(tmr, NULL, 0,   1);             // mode=1, count=0: 停止
 *   ddclose(tmr);
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. open 前必须设置 callback, 否则返回 -1
 * 2. 周期通过 ddwrite(mode=0) 设置, 默认 1000ms
 * 3. ddwrite(mode=1, count=1/0) 启动/停止
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_PC__
#include <pthread.h>
#include <windows.h>

typedef struct {
    volatile uint8_t running;
    pthread_t thread;
} timer_ctx_t;

static void* timer_loop(void* arg) {
    dd_t* dd = (dd_t*)arg;
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    uint16_t period = (uint16_t)(uintptr_t)dd->user_data;
    while (ctx->running) {
        Sleep(period);
        if (dd->callback) dd->callback(dd->user_data);
    }
    return NULL;
}

static int timer_open(dd_t* dd) {
    if (!dd->callback) return -1;
    if (!dd->user_data) dd->user_data = (void*)(uintptr_t)1000;

    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    if (!ctx) {
        ctx = (timer_ctx_t*)osmalloc(sizeof(timer_ctx_t));
        if (!ctx) return -1;
        dd->driver_data = ctx;
    }
    ctx->running = 1;
    pthread_create(&ctx->thread, NULL, timer_loop, dd);
    pthread_detach(ctx->thread);
    return 0;
}

static int timer_close(dd_t* dd) {
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    if (!ctx) return -1;
    ctx->running = 0;
    return 0;
}

static int timer_read(dd_t* dd, void* data, uint32_t size, uint32_t mode) {
    (void)dd; (void)mode;
    if (size >= sizeof(uint32_t)) *(uint32_t*)data = GetTickCount();
    return 0;
}

static const pdrv_ops_t timer_ops = {
    .ops_name = "clock",
    .open = timer_open,
    .close = timer_close,
    .read = timer_read,
};

REGISTER_DRIVER("tim_clocktask", NULL, &timer_ops, "TIM clock (PC)");

#endif
#if __USE_STM32__
#include "stm32f1xx.h"

static dd_t* tim_owners[4];

typedef struct {
    TIM_TypeDef*  tim;
    IRQn_Type     irqn;
    uint8_t       inst_no;
    volatile uint8_t running;
    uint16_t         period_ms;
} timer_ctx_t;

static void timer_enable_rcc(uint8_t inst_no)
{
    switch (inst_no) {
        case 1: RCC->APB2ENR |= RCC_APB2ENR_TIM1EN; break;
        case 2: RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; break;
        case 3: RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; break;
        case 4: RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; break;
    }
    (void)RCC->APB2ENR;
}

static int timer_probe(dd_t* dd)
{
    timer_ctx_t* ctx = (timer_ctx_t*)osmalloc(sizeof(timer_ctx_t));
    if (!ctx) return -1;
    ctx->tim = (TIM_TypeDef*)dd->dev->private->device_register;
    ctx->inst_no = dd->dev->private->inst_no;
    ctx->running = 0;
    ctx->period_ms = 1000;
    dd->driver_data = ctx;
    return 0;
}

static int timer_open(dd_t* dd)
{
    if (!dd->callback) return -1;
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;

    static const IRQn_Type irq_map[] = {
        TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn,
    };
    ctx->irqn = irq_map[ctx->inst_no - 1];

    timer_enable_rcc(ctx->inst_no);
    tim_owners[ctx->inst_no - 1] = dd;

    uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
    uint32_t arr = 10 * ctx->period_ms - 1;
    ctx->tim->PSC = (uint16_t)psc;
    ctx->tim->ARR = (uint16_t)arr;
    ctx->tim->EGR |= TIM_EGR_UG;
    ctx->tim->CNT = 0;
    ctx->tim->SR = ~TIM_SR_UIF;

    NVIC_SetPriority(ctx->irqn, 0);
    NVIC_EnableIRQ(ctx->irqn);
    return 0;
}

static int timer_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)data;
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;

    if (mode == 0) {
        ctx->period_ms = (uint16_t)count;
        uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
        uint32_t arr = 10 * ctx->period_ms - 1;
        ctx->tim->PSC = (uint16_t)psc;
        ctx->tim->ARR = (uint16_t)arr;
        ctx->tim->EGR |= TIM_EGR_UG;
    } else if (mode == 1) {
        if (count) {
            ctx->tim->CNT = 0;
            ctx->tim->SR = ~TIM_SR_UIF;
            ctx->tim->DIER |= TIM_DIER_UIE;
            ctx->running = 1;
            ctx->tim->CR1 |= TIM_CR1_CEN;
        } else {
            ctx->running = 0;
            ctx->tim->CR1 &= ~TIM_CR1_CEN;
            ctx->tim->DIER &= ~TIM_DIER_UIE;
        }
    }
    return 0;
}

static int timer_read(dd_t* dd, void* data, uint32_t size, uint32_t kreigster)
{
    (void)kreigster;
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    if (size >= sizeof(uint16_t))
        *(uint16_t*)data = ctx->period_ms;
    return 0;
}

static int timer_close(dd_t* dd)
{
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    if (!ctx) return -1;
    ctx->running = 0;
    ctx->tim->CR1 &= ~TIM_CR1_CEN;
    ctx->tim->DIER &= ~TIM_DIER_UIE;
    NVIC_DisableIRQ(ctx->irqn);
    tim_owners[ctx->inst_no - 1] = NULL;
    return 0;
}

static int timer_remove(dd_t* dd)
{
    if (dd->driver_data) {
        osfree(dd->driver_data);
        dd->driver_data = NULL;
    }
    return 0;
}

static const pdrv_ops_t timer_ops = {
    .ops_name = "clock",
    .open   = timer_open,
    .close  = timer_close,
    .write  = timer_write,
    .read   = timer_read,
};

static const pdrv_sysfunc_t timer_sysfunc = {
    .probe = timer_probe,
    .remove = timer_remove,
};

REGISTER_DRIVER("tim_clocktask", &timer_sysfunc, &timer_ops, "TIM clock source");

static void tim_irq_handler(int idx)
{
    dd_t* dd = tim_owners[idx];
    if (!dd) return;
    timer_ctx_t* ctx = (timer_ctx_t*)dd->driver_data;
    if (ctx->tim->SR & TIM_SR_UIF) {
        ctx->tim->SR = ~TIM_SR_UIF;
        if (ctx->running && dd->callback)
            dd->callback(dd->user_data);
    }
}

void TIM1_UP_IRQHandler(void) { tim_irq_handler(0); }
void TIM2_IRQHandler(void)    { tim_irq_handler(1); }
void TIM3_IRQHandler(void)    { tim_irq_handler(2); }
void TIM4_IRQHandler(void)    { tim_irq_handler(3); }

#endif
