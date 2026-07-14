#ifndef __KSCOSSYSTEM_H__
#define __KSCOSSYSTEM_H__

#include <stdint.h>
#include "KSCconfig.h"
#include "dd.h"

#define KSCOS_LOW_CLOCK 0
#define KSCOS_NORMAL_CLOCK 1
#define KSCOS_HIGH_CLOCK 2

extern volatile uint32_t KSCOSsystem_Clock;

#if __USE_STM32__
#include "app.h"
#include <stdarg.h>
extern app_t* ksc_console;
extern volatile uint32_t sys_tick_ms;
void kscprintf(const char* fmt, ...);
#else
extern dd_t* ksc_console;
#define kscprintf(fmt, ...) \
    do { if (ksc_console) ddioctl(ksc_console, fmt, ##__VA_ARGS__); } while(0)
#endif

void KSCOSSystemClock_Init(uint8_t clock_type);
void KSCOS_Error_Handler(void);
ki8 KSCOS_default_Error_Handler(void* data);
void sysdelay(uint32_t ms);
uint32_t sysgettime(void);

void* osmalloc(size_t size);
void osfree(void* ptr);
void* oscalloc(size_t num, size_t size);

void sys_init(void);

#endif
