/**
 * @file    ticker_demo.c
 * @note    周期时间戳打印应用 — 使用示例
 * 
 * ============================================================
 * 功能
 * ============================================================
 * 打开 ticker，通过 UART 观察周期输出。
 * 
 * ============================================================
 * 用法
 * ============================================================
 *   1. 替换 Core/Src/main.c 为本文件
 *   2. cmake --build --preset Debug
 *   3. 烧录后打开串口(115200)观察
 * 
 *   预期 UART 输出:
 *     ticker_demo: start
 *     ticker running for 5s...
 *     [1000] tick
 *     [2000] tick
 *     [3000] tick
 *     [4000] tick
 *     [5000] tick
 *     ticker_demo: done
 */

#include "master.h"
#include "app.h"
#include "KSCOSsystem.h"

int main(void)
{
    bus_init();
    sys_init();
    kscprintf("ticker_demo: start\r\n");

    app_t* tk = appget("ticker");
    if (!tk) { kscprintf("FAIL: no ticker\r\n"); while (1); }
    if (appopen(tk) < 0) { kscprintf("FAIL: appopen ticker\r\n"); while (1); }

    kscprintf("ticker running for 5s...\r\n");
    sysdelay(5000);

    appclose(tk);
    kscprintf("ticker_demo: done\r\n");

    while (1) sysdelay(1000);
}
