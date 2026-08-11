/**
 * @file    system_zone.c
 * @note    system app 固定区 (STM32) — 链接脚本 .system_zone 段
 *
 * app_t 放 .system_zone 段 (绝对固定地址), SYSTEMAPP 指向它。
 * 该段由 STM32F103XX_FLASH.ld 定义 (__system_zone_start)。
 */

#include "../../inc/app.h"
#include "../../inc/kscsystem.h"

/* app_t 本体放入 .system_zone 段 */
app_t ksc_system_app_mem __attribute__((section("system_zone"), used));

app_t* ksc_system_app = &ksc_system_app_mem;

void* system_zone_get(void)
{
    return (void*)&ksc_system_app_mem;
}
