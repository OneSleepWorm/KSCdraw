#include "../inc/KSCOSsystem.h"
#include "stdlib.h"

#if __USE_STM32__
app_t* ksc_console = NULL;
#else
dd_t*  ksc_console = NULL;
#endif
dd_t* ksc_timer = NULL;

void* osmalloc(size_t size)
{
    return malloc(size);
}

void osfree(void* ptr)
{
    free(ptr);
}

void* oscalloc(size_t num, size_t size)
{
    return calloc(num, size);
}

void sys_init(void)
{
    dd_t* clk = bus_getdriver("sys_clock");
    if (clk) { uint32_t m = 9; ddwrite(clk, &m, 4, 0); ddopen(clk); }

    ksc_timer = bus_getdriver("sys_time");
    if (ksc_timer) ddopen(ksc_timer);

#if __USE_STM32__
    ksc_console = appget("uart_serial");
    if (ksc_console) {
        ksc_console->user_data = (void*)1;
        appopen(ksc_console);
        appwrite(ksc_console, NULL, 0, 0x14);
    }
#else
    ksc_console = bus_getdriver(KSC_CONSOLE_DRIVER);
    if (ksc_console) ddopen(ksc_console);
#endif
}

#if __USE_STM32__
#include "stm32f1xx.h"
__volatile uint32_t KSCOSsystem_Clock = 0;

void KSCOSSystemClock_Init(unsigned char clock_type)
{
  (void)clock_type;
  KSCOSsystem_Clock = 8000000;
}

void KSCOS_Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    
  }
}
#endif
#if __USE_PC__
#include <stdio.h>
void KSCOS_Error_Handler(void)
{
  while (1)
  {
    
  }
}

void KSCOSSystemClock_Init(uint8_t clock_type){return;}
#endif
ki8 KSCOS_default_Error_Handler(void* data)
{
  KSCOS_Error_Handler();
  return -1;
}
