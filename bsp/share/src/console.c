/**
 * @file    console.c
 * @note    KSCOS console app — 固定地址全局加载路由
 *
 * console 是与 system 同待遇的固定地址 app, 承载 printf/终端等调试
 * 接口的全局路由。它不是 uart: 通过标准 app_dep 依赖 uart_serial
 * (app0), write/read 直接路由到 uart, 与普通 app 用法一致。
 *
 * kscprintf/kscterminal 经 CONSOLEAPP 固定句柄访问。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscsystem.h"

/* ================================================================
 * app 生命周期 (固定区, 非 osmalloc)
 * ================================================================ */
int console_app_open(app_t* app)
{
    /* app0 = uart_serial (REGISTER_APP_EX app_dep 注入), 无需额外处理 */
    if (!app->app0) return -1;
    return 0;
}

int console_app_close(app_t* app)
{
    (void)app;
    return -1;   /* console app 禁止关闭 */
}

int console_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app->app0) return -1;
    return appread(app->app0, data, count, mode);
}

int console_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app->app0) return -1;
    return appwrite(app->app0, data, count, mode);
}

int console_app_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)app; (void)cmdname; (void)argv;
    return -1;
}

static const papp_ops_t console_ops = {
    .open  = console_app_open,
    .close = console_app_close,
    .read  = console_app_read,
    .write = console_app_write,
    .cmd   = console_app_cmd,
};

REGISTER_APP_EX("console", "0", "1\0uart_serial", &console_ops,
    "KSCOS console router (fixed address, routes to uart_serial)");
