/**
 * @file    sys_console.c
 * @note    系统控制台驱动 (PC stdout / STM32 半主机)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 驱动名:  sys_console
 * ops_name: console
 * 功能:    格式化打印到调试终端
 * 
 * ┌──────────────┬──────────────────────────────────────────┐
 * │ 平台         │ 行为                                     │
 * ├──────────────┼──────────────────────────────────────────┤
 * │ PC           │ 调用 fwrite 输出到 stdout                │
 * │ STM32        │ 通过 ARM 半主机 (BKPT 0xAB) 输出到 烧录器 │
 * └──────────────┴──────────────────────────────────────────┘
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* con = bus_getdriver("sys", 0, "console");
 *   if (!con) while(1);
 *   ddioctl(con, "Hello %d\n", 123);
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 
 * ╔══════════════════════════════════════════════════════════╗
 * ║  STM32 半主机模式 — 极度不推荐用于实际项目               ║
 * ╠══════════════════════════════════════════════════════════╣
 * ║  1. BKPT 指令会暂停 CPU, 等待调试器响应, 速度极慢       ║
 * ║  2. 调试器断开时 BKPT → HardFault → 死机               ║
 * ║  3. 需开启 GDB Server + 启用 semihosting 才能看到输出   ║
 * ║  4. 只适合调试初期验证驱动链路, 正式开发请用 uart_printf ║
 * ╚══════════════════════════════════════════════════════════╝
 */

#include "../inc/dd.h"

#if __USE_PC__
#include <stdio.h>

static int console_ioctl(dd_t* dd, const char* fmt, va_list ap)
{
    (void)dd;
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        fwrite(buf, 1, len, stdout);
        fflush(stdout);
    }
    return n;
}

static const pdrv_ops_t console_ops = {
    .ioctl = console_ioctl,
};

REGISTER_DRIVER("sys_console", NULL, &console_ops, "Console I/O");

#endif

// STM32 半主机控制台已屏蔽 — bkpt 0xAB 会死机
// #if __USE_STM32__
// #include <stdio.h>
// ... 
// REGISTER_DRIVER("sys_console", NULL, &console_ops, "Console I/O");
// #endif
