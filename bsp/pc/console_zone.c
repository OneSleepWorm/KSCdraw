/* console_zone.c — console app 固定区 (PC) — 静态数组 */
#include "../../inc/app.h"
#include "../../inc/kscsystem.h"

/* 固定区: app_t + 余量 */
static uint8_t console_zone_mem[sizeof(app_t) + 256] __attribute__((aligned(8)));

app_t* ksc_console_app = (app_t*)console_zone_mem;

void* console_zone_get(void)
{
    return (void*)console_zone_mem;
}
