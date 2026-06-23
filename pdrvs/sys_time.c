/**
 * @file    sys_time.c
 * @note    系统时间驱动 (PC / STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  sys_time
 * 依赖:    无
 * 平台:    PC (__USE_PC__) / STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 提供毫秒级延时 (sysdelay) 和获取当前时间戳 (sysgettime)。
 * STM32 平台通过 SysTick_Handler 每 1ms 递增 sys_tick_ms。
 * PC 平台使用 Windows GetTickCount / Sleep。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   // 由 sys_init() 自动打开
 *   // 延时:
 *   sysdelay(100);  // 等待 100ms
 * 
 *   // 读时间戳:
 *   uint32_t t0 = sysgettime();
 *   // ... 做事 ...
 *   uint32_t elapsed = sysgettime() - t0;
 * 
 *   // 或通过 dd_t:
 *   dd_t* tmr = bus_getdriver("sys_time");
 *   ddopen(tmr);
 *   uint32_t now;
 *   ddread(tmr, &now, 0, 0);
 *   ddwrite(tmr, NULL, 100, 1);  // 延时 100ms
 * 
 * ============================================================
 * ddwrite / ddread mode 表
 * ============================================================
 * 操作 | mode | 功能
 * ------+------+-------------------------------
 * write |  1   | sysdelay(count) 毫秒
 * read  |  0   | 返回 sysgettime() 到 *data
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. STM32 依赖 SysTick 中断，需 sys_clock 先初始化 PLL
 * 2. sysdelay 为忙等待，会阻塞 CPU
 * 3. sys_tick_ms 为 volatile，可被中断直接读取
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"

#if __USE_STM32__
volatile uint32_t sys_tick_ms = 0;

void sysdelay(uint32_t ms)
{
    uint32_t start = sys_tick_ms;
    while (sys_tick_ms - start < ms);
}
uint32_t sysgettime(void)
{
    return sys_tick_ms;
}
#endif
#if __USE_PC__
#include <windows.h>
void sysdelay(uint32_t ms)
{
    Sleep(ms);
}
uint32_t sysgettime(void)
{
    return (uint32_t)GetTickCount();
}
#endif

static int systime_read(dd_t* dd, void* data, uint32_t count, uint32_t kreighter) {
    (void)dd; (void)count; (void)kreighter;
    *((uint32_t*)data) = sysgettime();
    return 0;
}
static int systime_write(dd_t* dd, void* data, uint32_t count, uint32_t mode) {
    (void)dd; (void)data;
    if (mode != 1) return 0;
    sysdelay(count);
    return 0;
}

static const pdrv_ops_t sys_drv_ops0 = {
    .open = OPEN_NULL_FUNC,
    .close = CLOSE_NULL_FUNC,
    .write = systime_write,
    .read = systime_read,
    .ioctl = IOCTL_NULL_FUNC,
};
REGISTER_DRIVER("sys_time", NULL, &sys_drv_ops0, "System time");
