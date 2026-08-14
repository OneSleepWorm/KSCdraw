#ifndef KSC_SYSTEM_H
#define KSC_SYSTEM_H

#include "app.h"
#include "KSCconfig.h"
#include "mempool.h"
#include <stdint.h>

/* ================================================================
 * KSCOS 系统 app (system) — 固定地址内核服务
 *
 * system 是一个特殊的 app: 固定地址 (SYSTEMAPP), 不走 osmalloc。
 * 提供时间/内存/初始化等系统服务, 普通 app 经 appcmd(SYSTEMAPP,...)
 * 或 appget("system") 访问。
 * ================================================================ */

/* system app 的 app_data 结构 (静态分配, 固定区) */
typedef struct {
    volatile uint32_t tick_ms;   /* SysTick 毫秒计数 (STM32) / GetTickCount 缓存 (PC) */
    uint32_t          clock;     /* 系统时钟 */
} ksc_system_data_t;

/* system app 命令 */
#define SYS_CMD_TIME    "time"
#define SYS_CMD_DELAY   "delay"
#define SYS_CMD_IDLE    "idle"
#define SYS_CMD_MALLOC  "malloc"
#define SYS_CMD_FREE    "free"
#define SYS_CMD_MEM     "mem"

/* ================================================================
 * SYSTEMAPP 固定地址
 * STM32: 链接脚本 .system_zone 段 (绝对固定)
 * PC:    静态全局数组 (逻辑固定, 运行时不变)
 * 由 bsp/<平台>/system_zone.c 提供
 * ================================================================ */
extern app_t* ksc_system_app;   /* 指向 system app (固定地址) */
#define SYSTEMAPP (ksc_system_app)

/* 系统 app 生命周期 (bsp/<平台>/system.c 实现, REGISTER_APP("system")) */
int  system_app_open(app_t* app);
int  system_app_close(app_t* app);
int  system_app_read(app_t* app, void* data, uint32_t count, uint32_t mode);
int  system_app_write(app_t* app, void* data, uint32_t count, uint32_t mode);
int  system_app_cmd(app_t* app, const char* cmdname, const char** argv);

/* 平台初始化 (system_app_open 内部调用) */
void system_platform_init(ksc_system_data_t* data);   /* 芯片时钟/外设初始化 */

/* 固定区定义 (bsp/<平台>/system_zone.c 实现) */
void* system_zone_get(void);          /* 返回固定区基址 (app_t*) */

/* ================================================================
 * KSCOS 控制台 app (console) — 固定地址全局加载路由
 *
 * console 是与 system 同待遇的固定地址 app: 全局加载路由, 承载
 * printf 输出 / 终端 IO 等调试接口。它不是 uart, 而是通过标准
 * app_dep 机制依赖 uart_serial (app0), 像普通 app 一样路由到 uart。
 *
 * kscprintf/kscterminal 经 CONSOLEAPP 固定句柄访问, 不再依赖运行时
 * ksc_console 全局变量。
 * ================================================================ */
extern app_t* ksc_console_app;   /* 指向 console app (固定地址) */
#define CONSOLEAPP (ksc_console_app)

/* console app 生命周期 (bsp/<平台>/console.c 实现, REGISTER_APP("console")) */
int  console_app_open(app_t* app);
int  console_app_close(app_t* app);
int  console_app_read(app_t* app, void* data, uint32_t count, uint32_t mode);
int  console_app_write(app_t* app, void* data, uint32_t count, uint32_t mode);
int  console_app_cmd(app_t* app, const char* cmdname, const char** argv);

/* 固定区定义 (bsp/<平台>/console_zone.c 实现) */
void* console_zone_get(void);      /* 返回 console 固定区基址 */

#endif /* KSC_SYSTEM_H */
