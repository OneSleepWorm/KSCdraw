#include "../inc/KSCOSsystem.h"
#include "../inc/kscsystem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================================================================
 * 共享实现 (所有平台)
 *
 * 说明: osmalloc/osfree/oscalloc/sysdelay/sysgettime/oswait_idle 已
 * 迁至 bsp/share/include/fastsystem.h (纯 static inline 宏封装,
 * 内部走 SYSTEMAPP 句柄 + appwrite/appread), 此处不再定义全局函数。
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
app_t* ksc_console = NULL;
app_t* ksc_term = NULL;
volatile uint32_t KSCOSsystem_Clock = 0;

void KSCOS_Error_Handler(void)
{
    while (1);
}

/* ================================================================
 * 控制台输出 (共享, 经 ksc_console 转发)
 * ================================================================ */
int __io_putchar(int ch)
{
    if (ksc_console && ksc_console->app_data) {
        char c = (char)ch;
        appwrite(ksc_console, &c, 1, 0);
    }
    return ch;
}

void kscprintf(const char* fmt, ...)
{
    if (!ksc_console) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[128];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        appwrite(ksc_console, buf, len, 0);
    }
}

int kscterminal(void)
{
    if (!ksc_term) return -1;
    uint8_t c;
    uint8_t r1;
    while (appread(ksc_console, &c, 1, 0) > 0)
        r1 = appwrite(ksc_term, &c, 1, 0);
    return r1;
}

/* ================================================================
 * 引导: 激活系统 app + 控制台/终端
 * ================================================================ */
void sys_init(void)
{
    app_t* uart = appget("uart_serial");
    if (uart) {
        appopen(uart);
        appcmd(uart, "open");   /* 打开默认通道 */
    }
    ksc_console = uart;

    app_t* term = appget("terminal");
    if (term) {
        appopen(term);
        ksc_term = term;
    }
}


