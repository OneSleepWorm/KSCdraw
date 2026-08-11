/**
 * @file    system.c
 * @note    KSCOS 系统 app — STM32 (时钟/时间/内存服务)
 *
 * 固定地址 app, 提供时间/内存/初始化等内核服务。
 * open 时做芯片初始化 (PLL/SysTick)。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscsystem.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "stm32f1xx.h"

/* 全局毫秒计数 (SysTick 中断递增, 保留全局便于中断 extern) */
volatile uint32_t sys_tick_ms = 0;

/* ================================================================
 * 芯片初始化 (PLL + SysTick)
 * ================================================================ */
static void stm32_pll_init(void)
{
    uint32_t mul = 9;
    uint32_t sysclk = 8000000 * mul;
    uint32_t latency;
    if (sysclk <= 24000000)
        latency = 0;
    else if (sysclk <= 48000000)
        latency = FLASH_ACR_LATENCY_1;
    else
        latency = FLASH_ACR_LATENCY_2;
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | latency;

    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    uint32_t pllmul = (mul - 2) << 18;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL))
              | RCC_CFGR_PLLSRC | pllmul;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW))
              | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1
              | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClock = sysclk;
    SysTick_Config(SystemCoreClock / 1000);
    NVIC_SetPriorityGrouping(4);
}

void system_platform_init(ksc_system_data_t* data)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR |= 1;
    __DSB();

    stm32_pll_init();

    data->clock = SystemCoreClock;
    sys_tick_ms = 0;         /* 全局计数 (SysTick 中断递增) */
    data->tick_ms = 0;
}

/* ================================================================
 * 时间服务
 * ================================================================ */
static uint32_t sys_gettime(void)
{
    return sys_tick_ms;
}

static void sys_delay(uint32_t ms)
{
    uint32_t start = sys_tick_ms;
    while (sys_tick_ms - start < ms);
}

static void sys_idle(void)
{
    __WFI();
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
    case 0:  /* 读 tick */
        *(uint32_t*)data = sys_gettime();
        return 4;
    case 1:  /* 读时钟 */
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
    case 0:  /* malloc: data=&ptr, count=size → *ptr = 分配地址 */
        if (!data || count == 0) return -1;
        {
            void** pp = (void**)data;
            *pp = mempool_alloc(count);
            return *pp ? 1 : -1;
        }
    case 1:  /* free: data=&ptr → 释放 *ptr */
        if (!data) return -1;
        {
            void** pp = (void**)data;
            mempool_free(*pp);
            *pp = NULL;
        }
        return 1;
    case 2:  /* delay: data=&ms */
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
        if (app->output_data) *(uint32_t*)app->output_data = d->tick_ms;
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
