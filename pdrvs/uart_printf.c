/**
 * @file    uart_printf.c
 * @note    USART1 串口打印驱动
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 驱动名:  uart_printf
 * ops_name: printf
 * 硬件:    USART1 — PA9(TX)  PA10(RX)  115200 8N1
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* uart = bus_getdriver("uart", 1, "printf");
 *   if (!uart) while(1);
 *   ddopen(uart);                              // 初始化硬件
 *   ddioctl(uart, "Hello %d\n", 123);          // 格式化打印
 *   ddwrite(uart, "raw", 3, 0);                // 发送原始数据
 *   ddclose(uart);                             // 关闭 USART1
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. BRR=625 对应 72MHz(APB2) ÷ 115200，时钟改变需重新计算
 * 2. 使能外设时钟后不能立即访问寄存器，必须插入延迟
 *    否则触发 STM32F1 总线错误 → HardFault
 * 3. ioctl 内部缓冲区 256 字节，超长内容会被截断
 * 4. 写操作是阻塞查询 TXE 标志，不会丢失数据
 * 5. 目前只支持 uart1，后续可扩展 dev_no 选择不同 UART 实例
 */

#include "../inc/dd.h"
#if __USE_STM32__
#include <stdio.h>
#include "main.h"

static int uart_printf_open(dd_t* dd)
{
    (void)dd;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    GPIOA->CRH = (GPIOA->CRH & ~(0xF << 4)) | (0xB << 4);
    GPIOA->CRH = (GPIOA->CRH & ~(0xF << 8)) | (0x4 << 8);

    USART1->BRR = 625;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    return 0;
}

static int uart_printf_close(dd_t* dd)
{
    (void)dd;
    USART1->CR1 &= ~USART_CR1_UE;
    return 0;
}

static int uart_printf_write(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    (void)dd; (void)mode;
    uint8_t* p = (uint8_t*)data;
    for (uint32_t i = 0; i < size; i++)
    {
        while (!(USART1->SR & USART_SR_TXE));
        USART1->DR = p[i];
    }
    return (int)size;
}

static int uart_printf_ioctl(dd_t* dd, const char* fmt, va_list ap)
{
    (void)dd;
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        for (size_t i = 0; i < len; i++)
        {
            while (!(USART1->SR & USART_SR_TXE));
            USART1->DR = (uint8_t)buf[i];
        }
    }
    return n;
}

static const driver_ops_t uart_printf_ops = {
    .ops_name = "printf",
    .open = uart_printf_open,
    .close = uart_printf_close,
    .write = uart_printf_write,
    .ioctl = uart_printf_ioctl,
};

REGISTER_DRIVER("uart_printf", &uart_printf_ops);

#endif
