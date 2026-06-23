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
 *   dd_t* tmr = bus_getdriver("tim_clock_2");
 *   if (!tmr) while(1);
 *   tmr->callback  = my_tick_callback;
 *   tmr->user_data = (void*)(uintptr_t)500;  // 周期 ms, 默认 1000
 *   ddopen(tmr);                              // 初始化, 不计数
 *   ddwrite(tmr, NULL, 200, 1);               // mode=1: 设周期 200ms
 *   ddwrite(tmr, NULL, 1,   2);               // mode=2, count=1: 启动
 *   // ... 定时器在后台运行 ...
 *   ddwrite(tmr, NULL, 0,   2);               // mode=2, count=0: 停止
 *   ddclose(tmr);
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. open 前必须设置 callback, 否则返回 -1
 * 2. 周期通过 ddwrite(mode=1) 设置, 默认 1000ms
 * 3. ddwrite(mode=2, count=1/0) 启动/停止
 * 4. mode=0 始终为 no-op
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
    .open = timer_open,
    .close = timer_close,
    .read = timer_read,
};

REGISTER_DRIVER("tim_clocktask", NULL, &timer_ops, "TIM clock (PC)");

#endif
#if __USE_STM32__
#include "stm32f1xx.h"

static dd_t* tim_owners[4];

static int reg_to_inst(uint32_t reg_base)
{
    switch (reg_base) {
        case 0x40012C00: return 1;
        case 0x40000000: return 2;
        case 0x40000400: return 3;
        case 0x40000800: return 4;
        default:         return 0;
    }
}

static int timer_open(dd_t* dd)
{
    if (!dd->callback) return -1;
    uint16_t period = dd->user_data ? (uint16_t)(uintptr_t)dd->user_data : 1000;

    uint32_t reg_base = dd->dev0->private->device_register;
    TIM_TypeDef* tim = (TIM_TypeDef*)reg_base;
    int inst_no = reg_to_inst(reg_base);

    static const IRQn_Type irq_map[] = {
        TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn,
    };
    IRQn_Type irqn = irq_map[inst_no - 1];

    tim_owners[inst_no - 1] = dd;

    uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
    uint32_t arr = 10 * period - 1;
    tim->PSC = (uint16_t)psc;
    tim->ARR = (uint16_t)arr;
    tim->EGR |= TIM_EGR_UG;
    tim->CNT = 0;
    tim->SR = ~TIM_SR_UIF;

    NVIC_SetPriority(irqn, 0);
    NVIC_EnableIRQ(irqn);
    return 0;
}

static int timer_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)data;
    TIM_TypeDef* tim = (TIM_TypeDef*)dd->dev0->private->device_register;

    if (mode == 1) {
        uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
        uint32_t arr = 10 * count - 1;
        tim->PSC = (uint16_t)psc;
        tim->ARR = (uint16_t)arr;
        tim->EGR |= TIM_EGR_UG;
    } else if (mode == 2) {
        if (count) {
            tim->CNT = 0;
            tim->SR = ~TIM_SR_UIF;
            tim->DIER |= TIM_DIER_UIE;
            tim->CR1 |= TIM_CR1_CEN;
        } else {
            tim->CR1 &= ~TIM_CR1_CEN;
            tim->DIER &= ~TIM_DIER_UIE;
        }
    }
    return 0;
}

static int timer_read(dd_t* dd, void* data, uint32_t size, uint32_t kreigster)
{
    (void)kreigster;
    TIM_TypeDef* tim = (TIM_TypeDef*)dd->dev0->private->device_register;
    if (size >= sizeof(uint16_t))
        *(uint16_t*)data = (tim->ARR + 1) / 10;
    return 0;
}

static int timer_close(dd_t* dd)
{
    uint32_t reg_base = dd->dev0->private->device_register;
    TIM_TypeDef* tim = (TIM_TypeDef*)reg_base;
    int inst_no = reg_to_inst(reg_base);

    tim->CR1 &= ~TIM_CR1_CEN;
    tim->DIER &= ~TIM_DIER_UIE;

    static const IRQn_Type irq_map[] = {
        TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn,
    };
    NVIC_DisableIRQ(irq_map[inst_no - 1]);
    tim_owners[inst_no - 1] = NULL;
    return 0;
}

static const pdrv_ops_t timer_ops = {
    .open   = timer_open,
    .close  = timer_close,
    .write  = timer_write,
    .read   = timer_read,
};

REGISTER_DRIVER("tim_clock_1", "1\0tim1", &timer_ops, "TIM1 clock");
REGISTER_DRIVER("tim_clock_2", "1\0tim2", &timer_ops, "TIM2 clock");
REGISTER_DRIVER("tim_clock_3", "1\0tim3", &timer_ops, "TIM3 clock");
REGISTER_DRIVER("tim_clock_4", "1\0tim4", &timer_ops, "TIM4 clock");

static void tim_irq_handler(int idx)
{
    dd_t* dd = tim_owners[idx];
    if (!dd) return;
    TIM_TypeDef* tim = (TIM_TypeDef*)dd->dev0->private->device_register;
    if (tim->SR & TIM_SR_UIF) {
        tim->SR = ~TIM_SR_UIF;
        if ((tim->CR1 & TIM_CR1_CEN) && dd->callback)
            dd->callback(dd->user_data);
    }
}

void TIM1_UP_IRQHandler(void) { tim_irq_handler(0); }
void TIM2_IRQHandler(void)    { tim_irq_handler(1); }
void TIM3_IRQHandler(void)    { tim_irq_handler(2); }
void TIM4_IRQHandler(void)    { tim_irq_handler(3); }

#endif
