#ifndef __KSCOSSYSTEM_H__
#define __KSCOSSYSTEM_H__

#include <stdint.h>
#include "KSCconfig.h"
#include "app.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KSCOS_LOW_CLOCK 0
#define KSCOS_NORMAL_CLOCK 1
#define KSCOS_HIGH_CLOCK 2

extern volatile uint32_t KSCOSsystem_Clock;
extern app_t* ksc_console;
extern app_t* ksc_term;
extern volatile uint32_t sys_tick_ms;   /* STM32: SysTick 递增; PC: 未用 */

void kscprintf(const char* fmt, ...);
void kscread(void* data);
int kscterminal(void);

void KSCOSSystemClock_Init(uint8_t clock_type);
void KSCOS_Error_Handler(void);
ki8 KSCOS_default_Error_Handler(void* data);
void sysdelay(uint32_t ms);
uint32_t sysgettime(void);

void* osmalloc(size_t size);
void osfree(void* ptr);
void* oscalloc(size_t num, size_t size);
void osdelay(uint32_t ms);
void oswait_idle(void);   /* 阻塞空闲等待: STM32=WFI 休眠, PC=让出 CPU */


void sys_init(void);

#ifdef __cplusplus
}
#endif

#endif
