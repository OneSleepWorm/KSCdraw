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
static int systime_write(dd_t* dd, void* data, uint32_t count, uint32_t kreighter) {
    (void)dd; (void)data; (void)kreighter;
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
REGISTER_DRIVER("sys_time", NULL, NULL, &sys_drv_ops0, "System time");
