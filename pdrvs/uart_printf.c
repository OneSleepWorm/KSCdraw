/**
 * @file    uart_printf.c
 * @note    UART 格式化输出驱动 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  uart_printf_1 / uart_printf_2 / uart_printf_3
 * 依赖:    uart1+gpioa / uart2+gpioa / uart3+gpiob
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 提供 UART 格式化打印功能，通过 kscprintf 宏间接调用。
 * 支持 3 路 UART，波特率固定 (UART1=115200, UART2/3=9600?)。
 * 自动配置 GPIO 为复用推挽输出 + 浮空输入，使能 USART。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   // 由 sys_init() 自动打开 KSC_CONSOLE_DRIVER
 *   // 之后可直接使用 kscprintf 宏:
 *   kscprintf("Hello %d\r\n", 123);
 * 
 *   // 或手动调用:
 *   dd_t* uart = bus_getdriver("uart_printf_1");
 *   if (uart) {
 *       ddopen(uart);
 *       ddwrite(uart, "Hello\n", 6, 0);
 *       ddioctl(uart, "value = %d\n", 42);
 *   }
 * 
 * ============================================================
 * kscprintf 宏 (KSCOSsystem.h)
 * ============================================================
 *   #define kscprintf(fmt, ...) \
 *       do { if (ksc_console) ddioctl(ksc_console, fmt, ##__VA_ARGS__); } while(0)
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 波特率由 BRR 寄存器硬编码:
 *    UART1 → BRR=625 (72MHz APB2 / 16 / 625 = 7200 bps?)
 *    实际: 72MHz / 16 / 625 = 7200, 不对 — 查手册
 *    BRR = 72MHz / 115200 = 625 (正确: 72000000 / (16*115200) = 39.06?
 *    总之该驱动写 625 用于 UART1 (APB2=72MHz), 312 用于 UART2/3 (APB1=36MHz)
 *    实际 BRR 计算: USARTDIV = PCLK / (16 * Baud)
 *    UART1: 72000000 / (16 * 115200) = 39.06 → BRR=39 → 实际 115384 baud
 *    UART2: 36000000 / (16 * 115200) = 19.53 → BRR=19 → 实际 118421 baud
 * 2. 使用 ddioctl + kscprintf 比 ddwrite 更便捷
 * 3. open 会自动配置 TX/RX 引脚和使能 USART
 * 4. mode=0 写操作仍会发送数据（未检查 mode）
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"
#include <stdio.h>

#define UART_REG(dd) ((USART_TypeDef*)(dd)->dev0->private->device_register)

static int uart_printf_open(dd_t* dd)
{
    uint32_t reg = dd->dev0->private->device_register;
    GPIO_TypeDef* gpio = (GPIO_TypeDef*)dd->dev1->private->device_register;
    USART_TypeDef* uart = UART_REG(dd);

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

    uint16_t brr = (reg == 0x40013800) ? 625 : 312;
    uart->BRR = brr;
    uart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    return 0;
}

static int uart_printf_close(dd_t* dd)
{
    UART_REG(dd)->CR1 &= ~USART_CR1_UE;
    return 0;
}

static int uart_printf_write(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    (void)mode;
    USART_TypeDef* uart = UART_REG(dd);
    uint8_t* p = (uint8_t*)data;
    for (uint32_t i = 0; i < size; i++)
    {
        while (!(uart->SR & USART_SR_TXE));
        uart->DR = p[i];
    }
    return (int)size;
}

static int uart_printf_ioctl(dd_t* dd, const char* fmt, va_list ap)
{
    USART_TypeDef* uart = UART_REG(dd);
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        for (size_t i = 0; i < len; i++)
        {
            while (!(uart->SR & USART_SR_TXE));
            uart->DR = (uint8_t)buf[i];
        }
    }
    return n;
}

static const pdrv_ops_t uart_printf_ops = {
    .open = uart_printf_open,
    .close = uart_printf_close,
    .write = uart_printf_write,
    .ioctl = uart_printf_ioctl,
};

REGISTER_DRIVER("uart_printf_1", "2\0uart1\0gpioa", &uart_printf_ops, "USART1 printf");
REGISTER_DRIVER("uart_printf_2", "2\0uart2\0gpioa", &uart_printf_ops, "USART2 printf");
REGISTER_DRIVER("uart_printf_3", "2\0uart3\0gpiob", &uart_printf_ops, "USART3 printf");

#endif
