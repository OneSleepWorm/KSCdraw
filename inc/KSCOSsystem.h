#ifndef __KSCOSSYSTEM_H__
#define __KSCOSSYSTEM_H__

#include <stdint.h>
#include "KSCconfig.h"
#include "app.h"
#include <stdarg.h>
#include "../bsp/share/include/fastsystem.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t KSCOSsystem_Clock;
extern app_t* ksc_console;
extern app_t* ksc_term;
extern volatile uint32_t sys_tick_ms;   /* STM32: SysTick 递增; PC: 未用 */

void kscprintf(const char* fmt, ...);
int kscterminal(void);

void KSCOS_Error_Handler(void);
ki8 KSCOS_default_Error_Handler(void* data);

void sys_init(void);

#ifdef __cplusplus
}
#endif

#endif
