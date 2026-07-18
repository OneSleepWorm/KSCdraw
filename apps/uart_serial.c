/**
 * @file    uart_serial.c
 * @note    UART 串口应用 — 统一 USART1/2/3, 依赖 gpio_port
 * @flash   ~1806B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  uart_serial
 * dep:     NULL
 * app_dep: "1\0gpio_port"
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用 (LTO差分法)
 * ============================================================
 *   ROM(Debug -O0):   2,128 B
 *   ROM(Release -Os):  1,092 B
 *   RAM(静态):   16 B (uart_owners[4])
 *   RAM(堆):     ~768 B (uart_rx_ring × 3, osmalloc 于 inst_init)
 *
 * ============================================================
 * appwrite — 发送/控制
 * ============================================================
 *   mode = (inst << 4) | op,  inst=1/2/3
 *
 *   op=1  data,count        轮询发送 count 字节
 *   op=2  count=1/0         开/关实例的 RXNE 中断
 *   op=3  data=&baud,count  设置波特率 (重新计算 BRR)
 *   op=4  —                 noop (初始化占位)
 *   op=5  count=1~3         设置 ioctl 默认实例
 *
 * ============================================================
 * appread — 接收
 * ============================================================
 *   mode 编码: bits[3:0]=inst, bit6=block, bit7=overflow
 *
 *   inst (1/2/3)       非阻塞读，无数据返回 0
 *   inst | 0x40        阻塞读，空转直到有数据
 *   inst | 0x80        读 overflow 计数 (count>0 自动清零)
 *
 * ============================================================
 * 回调 — RX 数据到达通知
 * ============================================================
 *   app->callback = on_rx;
 *   app->user_data = my_ctx;
 *   // ISR 中环空→非空时回调一次 (中断上下文)
 *
 * ============================================================
 * 典型用法
 * ============================================================
 *   app_t* u = appget("uart_serial");
 *   appopen(u);
 *   appwrite(u, "Hello\r\n", 7, 0x11);   // USART1 轮询发送
 *   appread(u, buf, 64, 1);               // USART1 非阻塞读
 *   appread(u, buf, 64, 0x41);            // USART1 阻塞读
 *   appclose(u);
 *
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"
#include <string.h>
#include <stdlib.h>

/* gpio_port mode constants (CRL/CRH nibble encoding) */
#define UART_TX_MODE  0x0B  /* push-pull output 50MHz */
#define UART_RX_MODE  0x04  /* floating input */

#define UART_RX_BUF_SIZE  256

typedef struct {
    volatile uint8_t  buf[UART_RX_BUF_SIZE];
    volatile uint16_t head, tail;
    volatile uint32_t overflow;
} uart_rx_ring_t;

typedef struct {
    uint8_t          enabled;       /* bit0=USART1 bit1=USART2 bit2=USART3 */
    uint8_t          dflt_inst;     /* ioctl 默认实例 */
    uint16_t         brr[3];        /* per-instance BRR value */
    uart_rx_ring_t*  ring[3];
    app_t*           owner[3];      /* ISR 反向查找 app_t */
    uint32_t         rd_val;
} uart_ctx_t;

static app_t* uart_owners[4];

static USART_TypeDef* uart_reg(uint8_t inst)
{
    static USART_TypeDef* const map[] = {USART1, USART2, USART3};
    if (inst < 1 || inst > 3) return NULL;
    return map[inst - 1];
}

static const IRQn_Type irq_map[] = {USART1_IRQn, USART2_IRQn, USART3_IRQn};

/* BRR = UART_CLK / baud.  USART1 on APB2, USART2/3 on APB1.
 * Default 115200: USART1=72M/115200=625, USART2/3=36M/115200≈312 */
#define UART_BAUD_DEFAULT  115200
static uint16_t uart_brr_compute(uint8_t inst, uint32_t baud)
{
    uint32_t pclk = (inst == 1) ? SystemCoreClock : (SystemCoreClock / 2);
    return (uint16_t)(pclk / baud);
}

static const uint32_t tx_pin[]  = {9, 2, 26};
static const uint32_t rx_pin[]  = {10, 3, 27};

static void rcc_enable(uint8_t inst)
{
    if (inst == 1) {
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    } else if (inst == 2) {
        RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    } else {
        RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    }
    (void)RCC->APB2ENR;
}

static void uart_tx_raw(USART_TypeDef* uart, const uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        while (!(uart->SR & USART_SR_TXE));
        uart->DR = buf[i];
    }
}

static void inst_init(app_t* app, uint8_t inst)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!ctx) return;
    uint8_t bit = 1 << (inst - 1);
    if (ctx->enabled & bit) return;

    ctx->ring[inst - 1] = osmalloc(sizeof(uart_rx_ring_t));
    if (!ctx->ring[inst - 1]) return;
    ctx->ring[inst - 1]->head = 0;
    ctx->ring[inst - 1]->tail = 0;
    ctx->ring[inst - 1]->overflow = 0;

    rcc_enable(inst);

    if (app->app0) appopen(app->app0);

    appwrite(app->app0, NULL, (tx_pin[inst - 1] << 4) | UART_TX_MODE, 1);
    appwrite(app->app0, NULL, (rx_pin[inst - 1] << 4) | UART_RX_MODE, 1);

    USART_TypeDef* uart = uart_reg(inst);
    uart->BRR = ctx->brr[inst - 1];
    uart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_SetPriority(irq_map[inst - 1], 0);
    NVIC_EnableIRQ(irq_map[inst - 1]);

    ctx->owner[inst - 1] = app;
    uart_owners[inst] = app;
    ctx->enabled |= bit;
}

static int uart_app_open(app_t* app)
{
    uart_ctx_t* ctx = (uart_ctx_t*)osmalloc(sizeof(uart_ctx_t));
    if (!ctx) return -1;
    ctx->enabled    = 0;
    ctx->dflt_inst  = 1;
    ctx->rd_val     = 0;
    for (int i = 0; i < 3; i++) {
        ctx->brr[i] = uart_brr_compute((uint8_t)(i + 1), UART_BAUD_DEFAULT);
        ctx->ring[i] = NULL;
        ctx->owner[i] = NULL;
    }
    app->app_data = ctx;
    app->callback_data = &ctx->rd_val;
    return 0;
}

static int uart_app_close(app_t* app)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!ctx) return -1;
    for (int i = 0; i < 3; i++) {
        if (ctx->enabled & (1 << i)) {
            USART_TypeDef* uart = uart_reg(i + 1);
            if (uart) uart->CR1 &= ~(USART_CR1_UE | USART_CR1_RXNEIE);
            NVIC_DisableIRQ(irq_map[i]);
            uart_owners[i + 1] = NULL;
        }
        if (ctx->ring[i]) osfree(ctx->ring[i]);
    }
    osfree(ctx);
    app->app_data = NULL;
    app->callback_data = NULL;
    return 0;
}

static int uart_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!ctx) return -1;

    if (mode == 0x05) {
        if (count >= 1 && count <= 3) ctx->dflt_inst = (uint8_t)count;
        return 1;
    }

    uint32_t inst = mode >> 4;
    uint32_t op   = mode & 0x0F;
    if (inst == 0) inst = ctx->dflt_inst;
    if (inst < 1 || inst > 3) return -1;

    inst_init(app, (uint8_t)inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;

    USART_TypeDef* uart = uart_reg((uint8_t)inst);

    switch (op) {
    case 0:
    case 1:
        if (!data || count == 0) return 0;
        uart_tx_raw(uart, (const uint8_t*)data, count);
        return (int)count;

    case 2:
        if (count)
            uart->CR1 |= USART_CR1_RXNEIE;
        else
            uart->CR1 &= ~USART_CR1_RXNEIE;
        return 1;

    case 3: {
        uint32_t baud = data ? *(uint32_t*)data : count;
        if (baud < 1200) return -1;
        ctx->brr[inst - 1] = uart_brr_compute((uint8_t)inst, baud);
        if (ctx->enabled & (1 << (inst - 1)))
            uart->BRR = ctx->brr[inst - 1];
        return 1;
    }

    case 4:
        return 1;

    default:
        return -1;
    }
}

static int uart_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!ctx || !data) return 0;

    uint32_t inst = mode & 0x0F;
    if (inst < 1 || inst > 3) return 0;
    if (!(ctx->enabled & (1 << (inst - 1)))) return 0;

    uart_rx_ring_t* r = ctx->ring[inst - 1];

    if (mode & 0x80) {
        *(uint32_t*)data = r->overflow;
        if (count) r->overflow = 0;
        return sizeof(uint32_t);
    }

    if (mode & 0x40)
        while (r->head == r->tail);

    if (!count) return 0;
    uint32_t read = 0;
    while (read < count && r->head != r->tail) {
        ((uint8_t*)data)[read++] = r->buf[r->tail];
        r->tail = (uint16_t)((r->tail + 1) & (UART_RX_BUF_SIZE - 1));
    }
    return (int)read;
}

static int cmd_open(app_t* app, const char** argv)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 3) return -1;
    inst_init(app, (uint8_t)inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    return 1;
}

static int cmd_close(app_t* app, const char** argv)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 3) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    USART_TypeDef* uart = uart_reg((uint8_t)inst);
    if (uart) uart->CR1 &= ~(USART_CR1_UE | USART_CR1_RXNEIE);
    NVIC_DisableIRQ(irq_map[inst - 1]);
    uart_owners[inst] = NULL;
    ctx->enabled &= ~(1 << (inst - 1));
    if (ctx->ring[inst - 1]) { osfree(ctx->ring[inst - 1]); ctx->ring[inst - 1] = NULL; }
    return 1;
}

static int cmd_baud(app_t* app, const char** argv)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i') || !APPCMD_HAS(argv, 'b')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    uint32_t baud = strtoul(argv[APPCMD_ARG('b')], NULL, 0);
    if (inst < 1 || inst > 3 || baud < 1200) return -1;
    ctx->brr[inst - 1] = uart_brr_compute((uint8_t)inst, baud);
    if (ctx->enabled & (1 << (inst - 1))) {
        USART_TypeDef* uart = uart_reg((uint8_t)inst);
        uart->BRR = ctx->brr[inst - 1];
    }
    return 1;
}

static int cmd_rxirq(app_t* app, const char** argv)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 3) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    USART_TypeDef* uart = uart_reg((uint8_t)inst);
    uint32_t en = APPCMD_HAS(argv, 'e') ? strtoul(argv[APPCMD_ARG('e')], NULL, 0) : 1;
    if (en)
        uart->CR1 |= USART_CR1_RXNEIE;
    else
        uart->CR1 &= ~USART_CR1_RXNEIE;
    return 1;
}

static int cmd_dflt(app_t* app, const char** argv)
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 3) return -1;
    ctx->dflt_inst = (uint8_t)inst;
    return 1;
}

static int cmd_baudrd(app_t* app, const char** argv)  /* rd */
{
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!app || !ctx || !APPCMD_HAS(argv, 'i')) return -1;
    uint32_t inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 3) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    uint32_t pclk = (inst == 1) ? SystemCoreClock : (SystemCoreClock / 2);
    uint32_t baud = pclk / ctx->brr[inst - 1];
    if (app->callback_data) *(uint32_t*)app->callback_data = baud;
    return 1;
}

typedef int (*uart_cmd_h)(app_t*, const char**);
typedef struct { const char* name; uart_cmd_h handler; } uart_cmd_t;

static const uart_cmd_t uart_cmds[] = {
    {"open",  cmd_open},
    {"close", cmd_close},
    {"baud",  cmd_baud},
    {"rxirq", cmd_rxirq},
    {"dflt",  cmd_dflt},
    {"rd",    cmd_baudrd},
    {NULL, NULL}
};

static int uart_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const uart_cmd_t* e = uart_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t uart_serial_ops = {
    .open  = uart_app_open,
    .close = uart_app_close,
    .write = uart_app_write,
    .read  = uart_app_read,
    .cmd   = uart_app_cmd,
};

REGISTER_APP_EX("uart_serial", "0", "1\0gpio_port", &uart_serial_ops,
    "Unified UART serial app (USART1/2/3)");

static void uart_irq_handler(int idx)
{
    app_t* app = uart_owners[idx + 1];
    if (!app) return;
    uart_ctx_t* ctx = (uart_ctx_t*)app->app_data;
    if (!ctx) return;

    USART_TypeDef* uart = uart_reg(idx + 1);
    if (uart->SR & USART_SR_RXNE) {
        uint8_t c = (uint8_t)uart->DR;
        uart_rx_ring_t* r = ctx->ring[idx];
        if (r) {
            uint8_t was_empty = (r->head == r->tail);
            uint16_t next = (uint16_t)((r->head + 1) & (UART_RX_BUF_SIZE - 1));
            if (next != r->tail) {
                r->buf[r->head] = c;
                r->head = next;
            } else {
                r->overflow++;
            }
            if (was_empty && app->callback)
                app->callback(app->user_data);
        }
    }
}

void USART1_IRQHandler(void) { uart_irq_handler(0); }
void USART2_IRQHandler(void) { uart_irq_handler(1); }
void USART3_IRQHandler(void) { uart_irq_handler(2); }

#elif __USE_PC__

#include <stdio.h>
#include <conio.h>

static int pc_uart_open(app_t* app)
{
    (void)app;
    setvbuf(stdout, NULL, _IONBF, 0);
    return 0;
}

static int pc_uart_close(app_t* app)
{
    (void)app;
    return 0;
}

static int pc_uart_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app;
    uint32_t op = mode & 0x0F;
    if (op == 0x01 || op == 0x11) {
        if (data && count > 0)
            fwrite(data, 1, count, stdout);
        return (int)count;
    }
    if (op == 0x04) return 1;
    return -1;
}

static int pc_uart_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app;
    if (!data || count == 0) return 0;
    if (mode & 0x40) {
        ((uint8_t*)data)[0] = (uint8_t)_getch();
        return 1;
    }
    if (!_kbhit()) return 0;
    ((uint8_t*)data)[0] = (uint8_t)_getch();
    return 1;
}

static int pc_uart_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)app;
    if (strcmp(cmdname, "print") == 0) {
        const char* msg = APPCMD_HAS(argv, 'm') ? argv[APPCMD_ARG('m')] : "";
        printf("%s\n", msg);
        return 0;
    }
    if (strcmp(cmdname, "clear") == 0) {
        printf("\033[2J\033[H");
        return 0;
    }
    if (strcmp(cmdname, "open") == 0 || strcmp(cmdname, "close") == 0)
        return 1;
    return -1;
}

static const papp_ops_t pc_uart_ops = {
    .open  = pc_uart_open,
    .close = pc_uart_close,
    .read  = pc_uart_read,
    .write = pc_uart_write,
    .cmd   = pc_uart_cmd,
};

REGISTER_APP_EX("uart_serial", "0", "0", &pc_uart_ops,
    "PC console via stdin/stdout");

#endif /* __USE_STM32__ / __USE_PC__ */
