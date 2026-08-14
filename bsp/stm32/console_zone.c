/* console_zone.c — console app 固定区 (STM32) — .console_zone 段 */
#include "../../inc/app.h"
#include "../../inc/kscsystem.h"

/* app_t 本体放入 .console_zone 段 (链接脚本定义) */
app_t ksc_console_app_mem __attribute__((section("console_zone"), used));

app_t* ksc_console_app = &ksc_console_app_mem;

void* console_zone_get(void)
{
    return (void*)&ksc_console_app_mem;
}
