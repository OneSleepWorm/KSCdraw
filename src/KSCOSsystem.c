#include "../inc/KSCOSsystem.h"
#include "../inc/kscsystem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================================================================
 * 共享实现 (所有平台) — 系统服务经 system app 分发
 * ================================================================ */

/* ================================================================
 * 内存服务 — 经 system app 分发 (mempool)
 * ================================================================ */

void* osmalloc(size_t size)
{
    void* p = NULL;
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &p, (uint32_t)size, 0);   /* mode=0: malloc */
    return p;
}

void osfree(void* ptr)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &ptr, sizeof(ptr), 1);    /* mode=1: free */
}

void* oscalloc(size_t num, size_t size)
{
    void* p = osmalloc(num * size);
    if (p) memset(p, 0, num * size);
    return p;
}

void osdelay(uint32_t ms) { sysdelay(ms); }

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

/* ================================================================
 * 系统服务 — 经 system app 二进制接口分发 (快, 无字符串解析)
 * ================================================================ */

void sysdelay(uint32_t ms)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &ms, sizeof(ms), 2);   /* mode=2: delay */
}

uint32_t sysgettime(void)
{
    uint32_t t = 0;
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->read)
        appread(SYSTEMAPP, &t, sizeof(t), 0);      /* mode=0: gettime */
    return t;
}

void oswait_idle(void)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, NULL, 0, 3);           /* mode=3: idle */
}

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
    appget("system");        /* system app 自动 open (芯片初始化) */

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


