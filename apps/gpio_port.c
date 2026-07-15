/**
 * @file    gpio_port.c
 * @note    统一 GPIO 端口 App — 全局引脚号直操寄存器
 * @flash   ~1304B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  gpio_port
 * 依赖:    无 (直接操作 stm32f1xx.h 寄存器宏)
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用 (LTO差分法)
 * ============================================================
 *   ROM(Debug -O0):   1,512 B
 *   ROM(Release -Os):   700 B
 *   RAM(静态):   0 B
 *   RAM(堆):     ~20 B (gpio_ctx_t, osmalloc 于 appopen)
 *
 * ============================================================
 * 引脚编号规则
 * ============================================================
 *   0-15   = GPIOA 0-15
 *   16-31  = GPIOB 0-15
 *   32-47  = GPIOC 0-15
 *   48-63  = GPIOD 0-15 (预留)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("gpio_port") → app_t*
 *   appopen(gpio)       : 分配上下文, RCC 懒初始化
 *   appclose(gpio)      : 释放内存
 *
 *   appwrite(gpio, data, count, mode):
 *     mode | data              | count              | 操作
 *     ------+------------------+--------------------+--------------------------
 *       0   | —                | —                  | no-op
 *       1   | —                | (pin<<4)|nibble    | CR 配置 (nibble=CNF<<2|MODE)
 *       2   | uint32_t 0/1     | pin                | BSRR 单引脚置/复位
 *       3   | uint32_t set32   | reset32            | 批量 BSRR (bit0-31=PA+PB)
 *       4   | —                | pin                | 翻转引脚
 *       5   | uint32_t 0/1/2   | pin                | 上下拉 (0=浮空,1=上拉,2=下拉)
 *       6   | uint32_t 16bit   | port_base 0/16/32  | 写 ODR (整端口)
 *
 *   appread(gpio, data, count, mode):
 *     mode | data              | count              | 操作
 *     ------+------------------+--------------------+--------------------------
 *       0   | —                | —                  | no-op
 *       1   | uint32_t* 16bit  | port_base 0/16/32  | 读整端口 IDR
 *       2   | uint32_t* 0/1    | pin                | 读单引脚 IDR
 *       3   | uint32_t* 16bit  | port_base 0/16/32  | 读整端口 ODR
 *
 * 典型用法:
 *   app_t* gpio = appget("gpio_port");
 *   appopen(gpio);
 *   appwrite(gpio, NULL, (4 << 4) | 0x3, 1);  // PA4 推挽输出 50MHz
 *   uint32_t v = 1; appwrite(gpio, &v, 4, 2);  // PA4 = HIGH
 *   v = 0; appwrite(gpio, &v, 4, 2);            // PA4 = LOW
 *   appread(gpio, &v, 0, 1);                    // v  = PORTA IDR
 *   appread(gpio, &v, 4, 2);                    // v  = PA4 电平 (0/1)
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <stdlib.h>
#include <string.h>
#if __USE_STM32__
#include "stm32f1xx.h"

#define PORT_OF(pin)  ((pin) >> 4)
#define PIN_OF(pin)   ((pin) & 0x0F)

typedef struct {
    uint8_t enabled_ports;  /* bit0=A bit1=B bit2=C bit3=D */
    uint32_t rd_val;
} gpio_ctx_t;

static GPIO_TypeDef* port_reg(uint8_t port)
{
    static GPIO_TypeDef* const map[] = {GPIOA, GPIOB, GPIOC, GPIOD};
    if (port > 3) return NULL;
    return map[port];
}

static void rcc_lazy(gpio_ctx_t* ctx, uint8_t port)
{
    uint8_t mask = 1 << port;
    if (ctx->enabled_ports & mask) return;
    ctx->enabled_ports |= mask;
    switch (port) {
    case 0: RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; break;
    case 1: RCC->APB2ENR |= RCC_APB2ENR_IOPBEN; break;
    case 2: RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; break;
#if defined(RCC_APB2ENR_IOPDEN)
    case 3: RCC->APB2ENR |= RCC_APB2ENR_IOPDEN; break;
#endif
    }
    (void)RCC->APB2ENR;
}

static int gpio_app_open(app_t* app)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)osmalloc(sizeof(gpio_ctx_t));
    if (!ctx) return -1;
    ctx->enabled_ports = 0;
    ctx->rd_val = 0;
    app->app_data = ctx;
    app->callback_data = &ctx->rd_val;
    return 0;
}

static int gpio_app_close(app_t* app)
{
    if (app->app_data) osfree(app->app_data);
    app->app_data = NULL;
    app->callback_data = NULL;
    return 0;
}

static int gpio_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!ctx) return 0;

    switch (mode) {

    case 0:
        return 1;

    case 1: {
        uint32_t pin = count >> 4;
        uint32_t nib = count & 0xF;
        uint8_t port = PORT_OF(pin);
        uint8_t local = PIN_OF(pin);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        volatile uint32_t* cr = (local < 8) ? &gpio->CRL : &gpio->CRH;
        uint32_t shift = (local & 7) * 4;
        *cr = (*cr & ~(0xF << shift)) | ((nib & 0xF) << shift);
        return 1;
    }

    case 2: {
        uint32_t pin = count;
        uint8_t port = PORT_OF(pin);
        uint8_t local = PIN_OF(pin);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        uint32_t val = data ? *(uint32_t*)data : 0;
        gpio->BSRR = val ? (1 << local) : (1 << (local + 16));
        (void)gpio->IDR;
        return 1;
    }

    case 3: {
        uint32_t set   = data ? *(uint32_t*)data : 0;
        uint32_t reset = count;
        rcc_lazy(ctx, 0);
        rcc_lazy(ctx, 1);
        GPIOA->BSRR = (set & 0x0000FFFF) | ((reset & 0x0000FFFF) << 16);
        GPIOB->BSRR = (set >> 16) | ((reset >> 16) << 16);
        return 1;
    }

    case 4: {
        uint32_t pin = count;
        uint8_t port = PORT_OF(pin);
        uint8_t local = PIN_OF(pin);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        uint32_t bit = gpio->IDR & (1 << local);
        gpio->BSRR = bit ? (1 << (local + 16)) : (1 << local);
        return 1;
    }

    case 5: {
        uint32_t pin = count;
        uint8_t port = PORT_OF(pin);
        uint8_t local = PIN_OF(pin);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        uint32_t val = data ? *(uint32_t*)data : 0;
        volatile uint32_t* cr = (local < 8) ? &gpio->CRL : &gpio->CRH;
        uint32_t shift = (local & 7) * 4;
        if (val == 0) {
            *cr = (*cr & ~(0xF << shift)) | (0x4 << shift);
        } else {
            *cr = (*cr & ~(0xF << shift)) | (0x8 << shift);
            if (val == 1)
                gpio->ODR |= (1 << local);
            else
                gpio->ODR &= ~(1 << local);
        }
        return 1;
    }

    case 6: {
        uint32_t port_base = count;
        uint8_t port = PORT_OF(port_base);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        uint32_t v = data ? *(uint32_t*)data : 0;
        gpio->ODR = v & 0xFFFF;
        return 1;
    }

    default:
        return 0;
    }
}

static int gpio_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!ctx || !data) return 0;

    switch (mode) {

    case 0:
        return 0;

    case 1: {
        uint8_t port = PORT_OF(count);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        *(uint32_t*)data = gpio->IDR;
        return 4;
    }

    case 2: {
        uint32_t pin = count;
        uint8_t port = PORT_OF(pin);
        uint8_t local = PIN_OF(pin);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        *(uint32_t*)data = (gpio->IDR >> local) & 1;
        return 4;
    }

    case 3: {
        uint8_t port = PORT_OF(count);
        GPIO_TypeDef* gpio = port_reg(port);
        if (!gpio) return 0;
        rcc_lazy(ctx, port);
        *(uint32_t*)data = gpio->ODR;
        return 4;
    }

    default:
        return 0;
    }
}

/* ================================================================
 * appcmd handlers — 通过 appcmd(gpio, "cfg -p N -m M") 调用
 *
 * 参数规则:
 *   -p <pin>    : 全局引脚号 (0-15=A, 16-31=B, 32-47=C)
 *   -v <0/1>    : 电平值 (set 用)
 *   -m <nibble> : CR 配置 nibble (cfg 用, 默认 0)
 *
 * 典型用法:
 *   appcmd(gpio, "cfg -p 4 -m 3");      // PA4 推挽输出 50MHz
 *   appcmd(gpio, "set -p 4 -v 1");      // PA4 = HIGH
 *   appcmd(gpio, "tog -p 4");            // PA4 翻转
 *   appcmd(gpio, "rd -p 4");            // 读 PA4 → callback_data
 * ================================================================ */

static int cmd_cfg(app_t* app, const char** argv)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'p')) return -1;
    uint32_t pin = strtoul(argv[APPCMD_ARG('p')], NULL, 0);
    uint32_t nib = APPCMD_HAS(argv, 'm') ? strtoul(argv[APPCMD_ARG('m')], NULL, 0) : 0;
    uint8_t port = PORT_OF(pin);
    uint8_t local = PIN_OF(pin);
    GPIO_TypeDef* gpio = port_reg(port);
    if (!gpio) return -1;
    rcc_lazy(ctx, port);
    volatile uint32_t* cr = (local < 8) ? &gpio->CRL : &gpio->CRH;
    uint32_t shift = (local & 7) * 4;
    *cr = (*cr & ~(0xF << shift)) | ((nib & 0xF) << shift);
    return 1;
}

/* set: 置/复位引脚 — -p pin -v 0/1 (默认 1=HIGH) */
static int cmd_set(app_t* app, const char** argv)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'p')) return -1;
    uint32_t pin = strtoul(argv[APPCMD_ARG('p')], NULL, 0);
    uint32_t val = APPCMD_HAS(argv, 'v') ? strtoul(argv[APPCMD_ARG('v')], NULL, 0) : 1;
    uint8_t port = PORT_OF(pin);
    uint8_t local = PIN_OF(pin);
    GPIO_TypeDef* gpio = port_reg(port);
    if (!gpio) return -1;
    rcc_lazy(ctx, port);
    gpio->BSRR = val ? (1 << local) : (1 << (local + 16));
    (void)gpio->IDR;
    return 1;
}

/* tog: 翻转引脚 — -p pin */
static int cmd_tog(app_t* app, const char** argv)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'p')) return -1;
    uint32_t pin = strtoul(argv[APPCMD_ARG('p')], NULL, 0);
    uint8_t port = PORT_OF(pin);
    uint8_t local = PIN_OF(pin);
    GPIO_TypeDef* gpio = port_reg(port);
    if (!gpio) return -1;
    rcc_lazy(ctx, port);
    uint32_t bit = gpio->IDR & (1 << local);
    gpio->BSRR = bit ? (1 << (local + 16)) : (1 << local);
    return 1;
}

/* rd: 读引脚电平 — -p pin, 结果存 callback_data */
static int cmd_rd(app_t* app, const char** argv)
{
    gpio_ctx_t* ctx = (gpio_ctx_t*)app->app_data;
    if (!app || !APPCMD_HAS(argv, 'p')) return -1;
    uint32_t pin = strtoul(argv[APPCMD_ARG('p')], NULL, 0);
    uint8_t port = PORT_OF(pin);
    uint8_t local = PIN_OF(pin);
    GPIO_TypeDef* gpio = port_reg(port);
    if (!gpio) return -1;
    rcc_lazy(ctx, port);
    if (app->callback_data)
        *(uint32_t*)app->callback_data = (gpio->IDR >> local) & 1;
    return 1;
}

/* appcmd dispatch table — 命令名 → handler */
typedef int (*gpio_cmd_h)(app_t*, const char**);
typedef struct { const char* name; gpio_cmd_h handler; } gpio_cmd_t;

static const gpio_cmd_t gpio_cmds[] = {
    {"cfg", cmd_cfg},   /* 配置引脚模式  -p pin -m nibble */
    {"set", cmd_set},   /* 置/复位引脚  -p pin -v 0/1 */
    {"tog", cmd_tog},   /* 翻转引脚     -p pin */
    {"rd",  cmd_rd},    /* 读引脚电平   -p pin → callback_data */
    {NULL, NULL}
};

static int gpio_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const gpio_cmd_t* e = gpio_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t gpio_app_ops = {
    .open  = gpio_app_open,
    .close = gpio_app_close,
    .write = gpio_app_write,
    .read  = gpio_app_read,
    .cmd   = gpio_app_cmd,
};

REGISTER_APP("gpio_port", "0", &gpio_app_ops,
    "Unified GPIO port driver (global pin# 0-15=A 16-31=B 32-47=C)");

#endif
