#ifndef __KSCOSSYSTEM_H__
#define __KSCOSSYSTEM_H__

#include <stdint.h>
#include "KSCconfig.h"
#include "dd.h"

#define KSCOS_LOW_CLOCK 0
#define KSCOS_NORMAL_CLOCK 1
#define KSCOS_HIGH_CLOCK 2

extern __volatile uint32_t KSCOSsystem_Clock;
extern dd_t* ksc_console;
extern dd_t* ksc_timer;

#define kscprintf(fmt, ...) \
    do { if (ksc_console) ddioctl(ksc_console, fmt, ##__VA_ARGS__); } while(0)
#define kscdelay(ms) \
    do { if (ksc_timer) ddwrite(ksc_timer, NULL, ms, 1); } while(0)
#define kscgettime() \
    ({ uint32_t _t = 0; if (ksc_timer) ddread(ksc_timer, &_t, 0, 0); _t; })

void KSCOSSystemClock_Init(uint8_t clock_type);
void KSCOS_Error_Handler(void);
ki8 KSCOS_default_Error_Handler(void* data);
void sysdelay(uint32_t ms);

void* osmalloc(size_t size);
void osfree(void* ptr);
void* oscalloc(size_t num, size_t size);

uint32_t sysgettime(void);
void sys_init(void);

#endif
