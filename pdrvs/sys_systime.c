/**
 * @file    sys_systime.c
 * @note    系统时间读写驱动
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 驱动名:  sys_systime
 * ops_name: systime
 * 功能:    获取系统滴答 / 延时等待
 * 平台:    PC + STM32 双平台
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* sys = bus_getdriver("sys", 0, "systime");
 *   if (!sys) while(1);
 *   uint32_t t;
 *   ddread(sys, &t, sizeof(t), 0);       // 获取系统时间 ms
 *   ddwrite(sys, NULL, 3000, 0);          // 延时 3000ms
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 读操作返回 sysgettime() -> HAL_GetTick
 * 2. 写操作传入 count 参数作为延时 ms
 * 3. data 指针在写操作中 unused, 传入 NULL 即可
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"

static int systime_read(dd_t* dd, void* data, uint32_t count, uint32_t kreighter) {
    *((uint32_t*)data) = sysgettime();
    return 0;
}
static int systime_write(dd_t* dd, void* data, uint32_t count, uint32_t kreighter) {
    sysdelay(count);
    return 0;
}

static const pdrv_ops_t sys_drv_ops0 = {
    .ops_name = "systime",
    .open = OPEN_NULL_FUNC,
    .close = CLOSE_NULL_FUNC,
    .write = systime_write,
    .read = systime_read,
    .ioctl = IOCTL_NULL_FUNC,
};
REGISTER_DRIVER("sys_systime", NULL, &sys_drv_ops0, "System time");
