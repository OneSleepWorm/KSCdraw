#include "../inc/KSCOSsystem.h"
#include "../inc/kscsystem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================================================================
 * 共享实现 (所有平台)
 *
 * 说明:
 *  - osmalloc/osfree/oscalloc/sysdelay/sysgettime/oswait_idle 已迁至
 *    bsp/share/include/fastsystem.h (纯 static inline 宏封装, 内部走
 *    SYSTEMAPP 句柄 + appwrite/appread), 此处不再定义全局函数。
 *  - kscprintf/kscterminal 经 CONSOLEAPP 固定句柄访问, 不再依赖运行时
 *    ksc_console/ksc_term 全局变量。
 * ================================================================ */

ki8 KSCOS_default_Error_Handler(void* data)
{
    (void)data;
    KSCOS_Error_Handler();
    return -1;
}

/* ================================================================
 * 全局 (共享定义)
 * ================================================================ */
volatile uint32_t KSCOSsystem_Clock = 0;

void KSCOS_Error_Handler(void)
{
    while (1);
}

/* ================================================================
 * 控制台输出 (共享, 经 CONSOLEAPP 固定句柄)
 * ================================================================ */
int __io_putchar(int ch)
{
    if (CONSOLEAPP && CONSOLEAPP->app_ops) {
        char c = (char)ch;
        appwrite(CONSOLEAPP, &c, 1, 0);
    }
    return ch;
}

void kscprintf(const char* fmt, ...)
{
    if (!CONSOLEAPP) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[128];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        appwrite(CONSOLEAPP, buf, len, 0);
    }
}

int kscterminal(void)
{
    app_t* term = appget("terminal");
    if (!term || !CONSOLEAPP) return -1;
    uint8_t c;
    uint8_t r1;
    while (appread(CONSOLEAPP, &c, 1, 0) > 0)
        r1 = appwrite(term, &c, 1, 0);
    return r1;
}

/* ================================================================
 * 引导: 激活固定 app + 控制台/终端
 * 起手招 (appget system/console) 由 main.c 显式调用, 此处只做依赖装配。
 * ================================================================ */
void sys_init(void)
{
    /* uart 通道初始化 (console 的路由目标, 引导层显式打开) */
    app_t* uart = appget("uart_serial");
    if (uart) {
        appopen(uart);
        appcmd(uart, "open");   /* 打开默认通道 */
    }

    app_t* console = appget("console");
    if (console)
        appopen(console);

    app_t* term = appget("terminal");
    if (term)
        appopen(term);
}


