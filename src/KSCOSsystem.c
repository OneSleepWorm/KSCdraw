#include "../inc/KSCOSsystem.h"
#include "stdlib.h"
#include <string.h>

#if __USE_STM32__
#include "stm32f1xx.h"
#include "app.h"

app_t* ksc_console = NULL;
volatile uint32_t sys_tick_ms = 0;
volatile uint32_t KSCOSsystem_Clock = 0;

void sysdelay(uint32_t ms)
{
    uint32_t start = sys_tick_ms;
    while (sys_tick_ms - start < ms);
}

uint32_t sysgettime(void)
{
    return sys_tick_ms;
}

static void pll_init(void)
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

    KSCOSsystem_Clock = sysclk;
    SystemCoreClock = sysclk;
    SysTick_Config(SystemCoreClock / 1000);
    NVIC_SetPriorityGrouping(4);
}

void sys_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR |= 1;
    __DSB();

    pll_init();

    ksc_console = appget("uart_serial");
    if (ksc_console) {
        ksc_console->user_data = (void*)1;
        appopen(ksc_console);
        appwrite(ksc_console, NULL, 0, 0x14);
    }
}

void KSCOSSystemClock_Init(unsigned char clock_type)
{
    (void)clock_type;
    KSCOSsystem_Clock = 8000000;
}

#else
dd_t*  ksc_console = NULL;

void sysdelay(uint32_t ms) { (void)ms; }
uint32_t sysgettime(void) { return 0; }

void sys_init(void)
{
    ksc_console = bus_getdriver(KSC_CONSOLE_DRIVER);
    if (ksc_console) ddopen(ksc_console);
}

void KSCOSSystemClock_Init(uint8_t clock_type) { (void)clock_type; }
#endif

void* osmalloc(size_t size) { return malloc(size); }
void osfree(void* ptr) { free(ptr); }
void* oscalloc(size_t num, size_t size) { return calloc(num, size); }

#if __USE_STM32__
void KSCOS_Error_Handler(void)
{
    __disable_irq();
    while (1);
}
#else
#include <stdio.h>
void KSCOS_Error_Handler(void)
{
    while (1);
}
#endif

ki8 KSCOS_default_Error_Handler(void* data)
{
    (void)data;
    KSCOS_Error_Handler();
    return -1;
}
