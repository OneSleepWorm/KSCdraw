/**
 * @file    sys_clock.c
 * @note    系统时钟初始化驱动 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  sys_clock
 * 依赖:    rcc + flash
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 配置 STM32F103 PLL 时钟：8MHz HSE × 倍频 → 72MHz SYSCLK。
 * 自动设置 AHB=72MHz, APB1=36MHz, APB2=72MHz, Flash 等待周期。
 * 初始化 SysTick (1ms)，设置 NVIC 优先级分组。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   // 由 sys_init() 自动调用:
 *   dd_t* clk = bus_getdriver("sys_clock");
 *   if (clk) { ddwrite(clk, &m, 4, 0); ddopen(clk); }
 *   // mode=0 的 ddwrite 不会生效, 但 ddopen 会应用默认倍频 x9
 * 
 *   // 手动切换倍频:
 *   uint32_t mul = 9;  // x9 = 72MHz
 *   ddwrite(clk, &mul, sizeof(mul), 1);
 * 
 *   // 读取当前系统时钟:
 *   uint32_t hz;
 *   ddread(clk, &hz, sizeof(hz), 0);
 * 
 * ============================================================
 * ddwrite / ddread mode 表
 * ============================================================
 * 操作 | mode | 功能
 * ------+------+-------------------------------
 * write |  1   | 用 *data 作为倍频系数重配 PLL
 * read  |  0   | 返回 KSCOSsystem_Clock 到 *data
 * 
 * ============================================================
 * 时钟树
 * ============================================================
 *   HSE(8MHz) → PLL(x9) → SYSCLK(72MHz)
 *                ├→ AHB(72MHz) → APB2(72MHz) → SPI1, USART1, ...
 *                │              → APB1(36MHz) → SPI2, USART2/3, TIM2/3/4, ...
 *                └→ SysTick(1ms)
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 必须在外置 HSE 晶振 (8MHz) 的板上使用，否则 HSEON 会卡死
 * 2. open 硬编码倍频 x9=72MHz；如需其他频率用 ddwrite(mode=1)
 * 3. 调用后重配 SysTick，会影响之前设置的时间基准
 * 4. sys_init 中先 ddwrite(mode=0) 是 no-op，仅 ddopen 生效
 * 5. 该驱动在所有应用/驱动之前打开
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

extern uint32_t SystemCoreClock;

static int sysclock_apply_pll(dd_t* dd, uint32_t mul)
{
    volatile uint32_t* rcc = (uint32_t*)dd->dev0->private->device_register;
    volatile uint32_t* flash = (uint32_t*)dd->dev1->private->device_register;

    if (mul < 2 || mul > 16) return -1;

    if (rcc[0] & RCC_CR_PLLON) {
        rcc[1] = (rcc[1] & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
        while ((rcc[1] & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE);
        rcc[0] &= ~RCC_CR_PLLON;
        while (rcc[0] & RCC_CR_PLLRDY);
    }

    uint32_t sysclk = 8000000 * mul;
    uint32_t latency;
    if (sysclk <= 24000000)
        latency = 0;
    else if (sysclk <= 48000000)
        latency = FLASH_ACR_LATENCY_1;
    else
        latency = FLASH_ACR_LATENCY_2;
    flash[0] = (flash[0] & ~FLASH_ACR_LATENCY) | latency;

    rcc[0] |= RCC_CR_HSEON;
    while (!(rcc[0] & RCC_CR_HSERDY));

    uint32_t pllmul = (mul - 2) << 18;
    rcc[1] = (rcc[1] & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL))
           | RCC_CFGR_PLLSRC | pllmul;

    rcc[0] |= RCC_CR_PLLON;
    while (!(rcc[0] & RCC_CR_PLLRDY));

    rcc[1] = (rcc[1] & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW))
           | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1
           | RCC_CFGR_SW_PLL;
    while ((rcc[1] & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    KSCOSsystem_Clock = sysclk;
    SystemCoreClock = sysclk;
    SysTick_Config(SystemCoreClock / 1000);
    return 0;
}

static int sysclock_open(dd_t* dd)
{
    NVIC_SetPriorityGrouping(4);
    *(volatile uint32_t*)0x40021018 |= (1 << 0);
    *(volatile uint32_t*)0x4002101C |= (1 << 28);
    *(volatile uint32_t*)0x40010004 |= (2 << 24);

    return sysclock_apply_pll(dd, 9);
}

static int sysclock_close(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int sysclock_write(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    if (mode != 1) return 0;
    if (!data || size < sizeof(uint32_t)) return -1;
    return sysclock_apply_pll(dd, *(uint32_t*)data);
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

REGISTER_DRIVER("sys_clock", "2\0rcc\0flash", &sysclock_ops, "System clock PLL init");

#endif
