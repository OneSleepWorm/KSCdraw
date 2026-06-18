/**
 * @file    gpio_keypad.c
 * @note    4×4 矩阵键盘扫描驱动 (寄存器级, 无 HAL)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 驱动名:  gpio_keypad
 * ops_name: keypad
 * 硬件:    PA0-PA3 (行输出)  PA4-PA7 (列输入, 下拉)
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* kpd = bus_getdriver("gpio", 1, "keypad");
 *   if (!kpd) while(1);
 *   ddopen(kpd);                            // GPIO 初始化
 *   // 在定时器回调中周期性调用:
 *   ddwrite(kpd, NULL, 0, 0);               // mode=0: 扫描一次, 结果入缓冲
 *   // 应用层:
 *   uint32_t key;
 *   ddread(kpd, &key, sizeof(key), 0);      // 从缓冲取一个结果, 0=空
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

typedef struct {
    uint32_t buf[4];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  count;
} keypad_ctx_t;

static uint32_t key_scan(void)
{
    uint32_t key = 0;

    GPIOA->BSRR = (0x0F << 16) | (1 << 0);
    key |= ((GPIOA->IDR >> 4) & 0x0F) << 0;

    GPIOA->BSRR = (0x0F << 16) | (1 << 1);
    key |= ((GPIOA->IDR >> 4) & 0x0F) << 4;

    GPIOA->BSRR = (0x0F << 16) | (1 << 2);
    key |= ((GPIOA->IDR >> 4) & 0x0F) << 8;

    GPIOA->BSRR = (0x0F << 16) | (1 << 3);
    key |= ((GPIOA->IDR >> 4) & 0x0F) << 12;

    return key;
}

static int keypad_open(dd_t* dd)
{
    (void)dd;
    keypad_ctx_t* ctx = (keypad_ctx_t*)osmalloc(sizeof(keypad_ctx_t));
    if (!ctx) return -1;
    ctx->head = 0; ctx->tail = 0; ctx->count = 0;
    dd->driver_data = ctx;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRL = 0x88881111;
    GPIOA->ODR &= ~0x00FF;

    return 0;
}

static int keypad_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)data; (void)count;
    if (mode != 0) return 0;

    keypad_ctx_t* ctx = (keypad_ctx_t*)dd->driver_data;
    uint32_t val = key_scan();

    if (ctx->count < 4) {
        ctx->buf[ctx->head] = val;
        ctx->head = (ctx->head + 1) & 3;
        ctx->count++;
    }
    return 0;
}

static int keypad_read(dd_t* dd, void* data, uint32_t size, uint32_t kreigster)
{
    (void)size; (void)kreigster;
    keypad_ctx_t* ctx = (keypad_ctx_t*)dd->driver_data;
    uint32_t val = 0;
    if (ctx->count > 0) {
        val = ctx->buf[ctx->tail];
        ctx->tail = (ctx->tail + 1) & 3;
        ctx->count--;
    }
    *(uint32_t*)data = val;
    return 0;
}

static int keypad_close(dd_t* dd)
{
    osfree(dd->driver_data);
    dd->driver_data = NULL;
    return 0;
}

static const driver_ops_t keypad_ops = {
    .ops_name = "keypad",
    .open   = keypad_open,
    .close  = keypad_close,
    .write  = keypad_write,
    .read   = keypad_read,
};

REGISTER_DRIVER("gpio_keypad", &keypad_ops);

#endif
