#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

extern uint32_t SystemCoreClock;

static int sysclock_open(dd_t* dd)
{
    uint32_t mul = dd->driver_data ? *(uint32_t*)dd->driver_data : 0;
    volatile uint32_t* rcc = (uint32_t*)dd->dev0->private->device_register;
    volatile uint32_t* flash = (uint32_t*)dd->dev1->private->device_register;

    NVIC_SetPriorityGrouping(4);
    *(volatile uint32_t*)0x40021018 |= (1 << 0);
    *(volatile uint32_t*)0x4002101C |= (1 << 28);
    *(volatile uint32_t*)0x40010004 |= (2 << 24);

    if (mul < 2 || mul > 16) {
        uint32_t def = 9;
        ddwrite(dd, &def, 4, 0);
        mul = 9;
    }

    rcc[0] |= RCC_CR_HSEON;
    while (!(rcc[0] & RCC_CR_HSERDY));

    uint32_t pllmul = (mul - 2) << 18;
    rcc[1] = (rcc[1] & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL))
           | RCC_CFGR_PLLSRC | pllmul;

    rcc[0] |= RCC_CR_PLLON;
    while (!(rcc[0] & RCC_CR_PLLRDY));

    uint32_t sysclk = 8000000 * mul;
    uint32_t latency;
    if (sysclk <= 24000000)
        latency = 0;
    else if (sysclk <= 48000000)
        latency = FLASH_ACR_LATENCY_1;
    else
        latency = FLASH_ACR_LATENCY_2;
    flash[0] = (flash[0] & ~FLASH_ACR_LATENCY) | latency;

    rcc[1] = (rcc[1] & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW))
           | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1
           | RCC_CFGR_SW_PLL;
    while ((rcc[1] & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    KSCOSsystem_Clock = sysclk;
    SystemCoreClock = sysclk;
    SysTick_Config(SystemCoreClock / 1000);
    return 0;
}

static int sysclock_close(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int sysclock_write(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    (void)mode;
    if (data && size >= sizeof(uint32_t))
        *(uint32_t*)dd->driver_data = *(uint32_t*)data;
    return 0;
}

static int sysclock_read(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    (void)dd; (void)mode;
    if (data && size >= sizeof(uint32_t))
        *(uint32_t*)data = KSCOSsystem_Clock;
    return 0;
}

static const pdrv_ops_t sysclock_ops = {
    .open   = sysclock_open,
    .close  = sysclock_close,
    .write  = sysclock_write,
    .read   = sysclock_read,
};

REGISTER_DRIVER("sys_clock", "2\0rcc\0flash", NULL, &sysclock_ops, "System clock PLL init");

#endif
