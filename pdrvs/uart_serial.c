/**
 * @file    uart_serial.c
 * @note    UART 串口收发驱动 (TX 轮询 + RX 中断) (STM32)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  uart_serial_1 / uart_serial_2 / uart_serial_3
 * 依赖:    uart1+gpioa / uart2+gpioa / uart3+gpiob
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用
 * ============================================================
 *   ROM(Debug -O0):   N B (待测)
 *   ROM(Release -Os):  N B (待测)
 *   RAM(静态):  12 B (uart_owners[3])
 *   RAM(堆):    264 B (uart_serial_ctx_t)
 *   RAM(栈):    由链接脚本统一分配
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * ddopen(dd)
 *   前置条件: dd 已通过 bus_getdriver 获取.
 *   配置 GPIO TX(AF-PP)/RX(浮空输入), 设置波特率, 使能 USART,
 *   使能 RXNE 中断, 分配并初始化环形缓冲区.
 *
 * ddclose(dd)
 *   关闭 USART, 禁用 NVIC, 释放环形缓冲区及上下文.
 *
 * ddwrite(dd, data, count, mode)
 *   0  — no-op (通用约定)
 *   1  — TX 发送. data=字节流指针, count=发送长度.
 *   2  — RX 中断控制. count=1 使能 RXNEIE, count=0 禁用.
 *
 * ddread(dd, data, count, mode)
 *   0  — no-op (通用约定)
 *   1  — 批量读取. data=输出缓冲, count=最大读取长度.
 *        返回实际读取的字节数 (0~count).
 *   2  — 查询可读字节数. 返回环形缓冲区中可用字节数.
 *        若 data!=NULL, 将计数写入 *(uint32_t*)data.
 *   3  — 单字节读取. data=uint8_t*, 返回 1=有数据, 0=空.
 *
 * ddioctl(dd, fmt, ...)
 *   格式化输出 (同 uart_printf). 内部调用 vsnprintf + 轮询 TX.
 *
 * callback
 *   若设置, 每次 RXNE 中断收完一个字节后从 ISR 调用.
 *   callback(user_data).
 *
 * 典型使用:
 *
 *   dd_t* uart = bus_getdriver("uart_serial_2");
 *   if (!uart) while(1);
 *   ddopen(uart);
 *
 *   // TX: 发送数据
 *   ddwrite(uart, "AT\r\n", 4, 1);
 *
 *   // RX: 查询并读取
 *   if (ddread(uart, NULL, 0, 2) > 0) {
 *       uint8_t buf[64];
 *       int n = ddread(uart, buf, 64, 1);
 *   }
 *
 *   // 单字节读取
 *   uint8_t c;
 *   if (ddread(uart, &c, 1, 3) > 0) { ... }
 *
 *   ddclose(uart);
 *
 * ============================================================
 * 扩展预留说明
 * ============================================================
 * 1. uart_serial_ctx_t 末尾可追加 DMA 通道号 / IDLE 标志等字段
 * 2. ISR 中预留 IDLE 中断处理分支 (USART_SR_IDLE)
 * 3. ddwrite/ddread 的 mode 10+ 留作 DMA 批量操作
 * 4. CR3 的 DMAR/DMAT 位可随时使能以启用 DMA 传输
 * ============================================================
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"
#include "stm32f103xb.h"
#include <stdio.h>

#define UART_RX_BUF_SIZE  256
#define UART_INST_COUNT   3

#define UART_REG(dd)  ((USART_TypeDef*)(dd)->dev0->private->device_register)

typedef struct {
    volatile uint8_t  buf[UART_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_rx_ring_t;

typedef struct {
    uart_rx_ring_t ring;
    /* ========== 预留扩展区 ========== */
    /* uint8_t  dma_ch;               */
    /* uint8_t  idle_enabled;         */
    /* volatile uint32_t frame_len;   */
} uart_serial_ctx_t;

static dd_t* uart_owners[UART_INST_COUNT];

static const IRQn_Type irq_map[UART_INST_COUNT] = {
    USART1_IRQn,
    USART2_IRQn,
    USART3_IRQn,
};

static int reg_to_inst(uint32_t reg)
{
    switch (reg) {
        case 0x40013800: return 0;
        case 0x40004400: return 1;
        case 0x40004800: return 2;
        default:         return -1;
    }
}

static inline uint16_t ring_available(uart_rx_ring_t* ring)
{
    return (uint16_t)((ring->head - ring->tail) & (UART_RX_BUF_SIZE - 1));
}

static inline int ring_read_byte(uart_rx_ring_t* ring, uint8_t* c)
{
    if (ring->head == ring->tail) return 0;
    *c = ring->buf[ring->tail];
    ring->tail = (uint16_t)((ring->tail + 1) & (UART_RX_BUF_SIZE - 1));
    return 1;
}

static void gpio_config(USART_TypeDef* uart, GPIO_TypeDef* gpio, uint32_t reg)
{
    if (reg == 0x40013800) {
        gpio->CRH = (gpio->CRH & ~(0xF << 4)) | (0xB << 4);
        gpio->CRH = (gpio->CRH & ~(0xF << 8)) | (0x4 << 8);
    } else if (reg == 0x40004400) {
        gpio->CRL = (gpio->CRL & ~(0xF << 8)) | (0xB << 8);
        gpio->CRL = (gpio->CRL & ~(0xF << 12)) | (0x4 << 12);
    } else {
        gpio->CRH = (gpio->CRH & ~(0xF << 8)) | (0xB << 8);
        gpio->CRH = (gpio->CRH & ~(0xF << 12)) | (0x4 << 12);
    }
}

static int uart_serial_open(dd_t* dd)
{
    uint32_t reg = dd->dev0->private->device_register;
    int inst = reg_to_inst(reg);
    if (inst < 0) return -1;

    USART_TypeDef* uart = UART_REG(dd);
    GPIO_TypeDef*  gpio = (GPIO_TypeDef*)dd->dev1->private->device_register;

    gpio_config(uart, gpio, reg);

    uint16_t brr = (reg == 0x40013800) ? 625 : 312;
    uart->BRR = brr;

    uart_serial_ctx_t* ctx = osmalloc(sizeof(uart_serial_ctx_t));
    if (!ctx) return -1;
    ctx->ring.head = 0;
    ctx->ring.tail = 0;
    dd->user_data = ctx;

    uart_owners[inst] = dd;

    uart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_SetPriority(irq_map[inst], 0);
    NVIC_EnableIRQ(irq_map[inst]);

    return 0;
}

static int uart_serial_close(dd_t* dd)
{
    if (!dd) return -1;

    uint32_t reg = dd->dev0->private->device_register;
    int inst = reg_to_inst(reg);
    USART_TypeDef* uart = UART_REG(dd);

    uart->CR1 &= ~(USART_CR1_UE | USART_CR1_RXNEIE);

    if (inst >= 0 && inst < UART_INST_COUNT) {
        NVIC_DisableIRQ(irq_map[inst]);
        uart_owners[inst] = NULL;
    }

    if (dd->user_data) {
        osfree(dd->user_data);
        dd->user_data = NULL;
    }

    return 0;
}

static int uart_serial_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    USART_TypeDef* uart = UART_REG(dd);

    switch (mode) {
    case 0:
        return 0;

    case 1:
    {
        uint8_t* p = (uint8_t*)data;
        for (uint32_t i = 0; i < count; i++) {
            while (!(uart->SR & USART_SR_TXE));
            uart->DR = p[i];
        }
        return (int)count;
    }

    case 2:
        if (count)
            uart->CR1 |= USART_CR1_RXNEIE;
        else
            uart->CR1 &= ~USART_CR1_RXNEIE;
        return 0;

    default:
        return -1;
    }
}

static int uart_serial_read(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    uart_serial_ctx_t* ctx = (uart_serial_ctx_t*)dd->user_data;
    if (!ctx) return -1;

    switch (mode) {
    case 0:
        return 0;

    case 1:
    {
        if (!data || !count) return 0;
        uint32_t read = 0;
        while (read < count) {
            if (!ring_read_byte(&ctx->ring, &((uint8_t*)data)[read]))
                break;
            read++;
        }
        return (int)read;
    }

    case 2:
    {
        uint16_t avail = ring_available(&ctx->ring);
        if (data) *(uint32_t*)data = avail;
        return (int)avail;
    }

    case 3:
    {
        if (!data) return 0;
        return ring_read_byte(&ctx->ring, (uint8_t*)data);
    }

    default:
        return -1;
    }
}

static int uart_serial_ioctl(dd_t* dd, const char* fmt, va_list ap)
{
    USART_TypeDef* uart = UART_REG(dd);
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        for (size_t i = 0; i < len; i++) {
            while (!(uart->SR & USART_SR_TXE));
            uart->DR = (uint8_t)buf[i];
        }
    }
    return n;
}

static const pdrv_ops_t uart_serial_ops = {
    .open  = uart_serial_open,
    .close = uart_serial_close,
    .write = uart_serial_write,
    .read  = uart_serial_read,
    .ioctl = uart_serial_ioctl,
};

REGISTER_DRIVER("uart_serial_1", "2\0uart1\0gpioa", &uart_serial_ops, "USART1 serial");
REGISTER_DRIVER("uart_serial_2", "2\0uart2\0gpioa", &uart_serial_ops, "USART2 serial");
REGISTER_DRIVER("uart_serial_3", "2\0uart3\0gpiob", &uart_serial_ops, "USART3 serial");

static void uart_irq_handler(int idx)
{
    dd_t* dd = uart_owners[idx];
    if (!dd) return;

    USART_TypeDef* uart = (USART_TypeDef*)dd->dev0->private->device_register;
    uart_serial_ctx_t* ctx = (uart_serial_ctx_t*)dd->user_data;
    if (!ctx) return;

    if (uart->SR & USART_SR_RXNE) {
        uint8_t c = (uint8_t)uart->DR;
        uint16_t next = (ctx->ring.head + 1) & (UART_RX_BUF_SIZE - 1);
        if (next != ctx->ring.tail) {
            ctx->ring.buf[ctx->ring.head] = c;
            ctx->ring.head = next;
        }
        if (dd->callback)
            dd->callback(dd->user_data);
    }

    /* ========== 预留: IDLE 中断扩展 ==========
    if (uart->SR & USART_SR_IDLE) {
        (void)uart->DR;
        if (dd->callback)
            dd->callback(dd->user_data);
    }
    */
}

void USART1_IRQHandler(void) { uart_irq_handler(0); }
void USART2_IRQHandler(void) { uart_irq_handler(1); }
void USART3_IRQHandler(void) { uart_irq_handler(2); }

#endif
