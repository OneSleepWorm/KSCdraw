#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"
#include <stdio.h>

typedef struct {
    USART_TypeDef* uart;
} uart_ctx_t;

static int uart_printf_open(dd_t* dd)
{
    uart_ctx_t* ctx = (uart_ctx_t*)dd->driver_data;
    ctx->uart = (USART_TypeDef*)dd->dev0->private->device_register;
    uint32_t reg = dd->dev0->private->device_register;
    GPIO_TypeDef* gpio = (GPIO_TypeDef*)dd->dev1->private->device_register;

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
    ctx->uart->BRR = brr;
    ctx->uart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    return 0;
}

static int uart_printf_close(dd_t* dd)
{
    uart_ctx_t* ctx = (uart_ctx_t*)dd->driver_data;
    if (!ctx) return -1;
    ctx->uart->CR1 &= ~USART_CR1_UE;
    return 0;
}

static int uart_printf_write(dd_t* dd, void* data, uint32_t size, uint32_t mode)
{
    (void)mode;
    uart_ctx_t* ctx = (uart_ctx_t*)dd->driver_data;
    uint8_t* p = (uint8_t*)data;
    for (uint32_t i = 0; i < size; i++)
    {
        while (!(ctx->uart->SR & USART_SR_TXE));
        ctx->uart->DR = p[i];
    }
    return (int)size;
}

static int uart_printf_ioctl(dd_t* dd, const char* fmt, va_list ap)
{
    uart_ctx_t* ctx = (uart_ctx_t*)dd->driver_data;
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        for (size_t i = 0; i < len; i++)
        {
            while (!(ctx->uart->SR & USART_SR_TXE));
            ctx->uart->DR = (uint8_t)buf[i];
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

REGISTER_DRIVER("uart_printf_1", "2\0uart1\0gpioa", NULL, &uart_printf_ops, "USART1 printf");
REGISTER_DRIVER("uart_printf_2", "2\0uart2\0gpioa", NULL, &uart_printf_ops, "USART2 printf");
REGISTER_DRIVER("uart_printf_3", "2\0uart3\0gpiob", NULL, &uart_printf_ops, "USART3 printf");

#endif
