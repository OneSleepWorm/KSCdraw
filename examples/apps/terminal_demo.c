/**
 * @file    terminal_demo.c
 * @note    终端 App — 使用示例 (UART 交互)
 *
 * ============================================================
 * 功能
 * ============================================================
 * 初始化终端，通过 appwrite 喂入命令字符串，
 * 终端自动解析 appname 并路由到对应 app 的 appcmd。
 * idle loop 轮询 UART RX 字节喂给终端 (raw byte 模式)。
 *
 * ============================================================
 * 用法
 * ============================================================
 *   1. 替换 Core/Src/main.c 为本文件
 *   2. cmake --build --preset Debug
 *   3. 烧录后打开串口(115200)观察
 *
 *   预期 UART 输出:
 *     terminal ready
 *     apps:
 *       gpio_port
 *       uart_serial
 *       ...
 *     hello world
 *     gpio_port: 0
 *     ...
 *
 *   串口交互 (烧录后可打字):
 *     > help       ← 列出所有 app
 *     > echo hi    ← 回显 "hi"
 *     > gpio_port  ← 路由到 gpio_port
 *
 * ============================================================
 * 接口说明
 * ============================================================
 *   appwrite(term, data, len, 0)   — raw bytes 流 (攒行)
 *   appwrite(term, data, len, 1)   — 完整命令 (直接路由)
 *   app->callback(str)             — 输出回调 (默认→ksc_console)
 *
 *   命令格式:  appname subcmd -x val -y val
 *   内建:      help, echo
 */

#include "app.h"
#include "KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

int main(void)
{
    sys_init();

    /* ── 终端初始化 ── */
    app_t* term = appget("terminal");
    if (term) appopen(term);

    /* ── 测试：通过 appwrite 喂命令 ── */

    /* built-in: help */
    appwrite(term, "help\r\n", 6, 1);

    /* built-in: echo */
    appwrite(term, "echo hello world\r\n", 18, 1);

    /* 路由到 gpio_port (验证 appget → appopen → appcmd 链路) */
    appwrite(term, "gpio_port\r\n", 11, 1);

    /* raw byte 模式: 分两次发送凑成 "echo ok" */
    appwrite(term, "ec", 2, 0);
    appwrite(term, "ho ok\r\n", 7, 0);

    /* ── idle loop: UART RX → terminal ── */
    while (1) {
        uint8_t c;
        while (appread(ksc_console, &c, 1, 1) > 0)
            appwrite(term, &c, 1, 0);
    }
}
