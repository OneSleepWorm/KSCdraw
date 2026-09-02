/**
 * @file    system_zone.c
 * @note    system app 固定区 (Linux) — 静态全局数组, 逻辑固定地址
 *
 * Linux 无物理地址锁定, 用静态数组 (进程生命周期内地址固定)。
 * SYSTEMAPP 指向该数组起始 (解释为 app_t)。
 *
 * 与 bsp/pc/system_zone.c 完全一致 — 无任何平台依赖, gcc 原生支持
 * __attribute__((aligned(8)))。
 */

#include "../../inc/app.h"
#include "../../inc/kscsystem.h"
#include <string.h>

/* 固定区: app_t + system data + 余量 (后续可放内核保留结构) */
static uint8_t system_zone_mem[sizeof(app_t) + 512] __attribute__((aligned(8)));

app_t* ksc_system_app = (app_t*)system_zone_mem;

void* system_zone_get(void)
{
    return (void*)system_zone_mem;
}
