/**
 * @file    gpio_port.c
 * @note    GPIO 端口读写驱动 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  gpio_port_a / gpio_port_b / gpio_port_c
 * 依赖:    gpioa / gpiob / gpioc
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 提供 GPIO 端口级别的引脚模式配置、单引脚写、批量 BSRR 写、
 * 以及端口 IDR 读取。常用于初始化外设复用引脚或读取按键电平。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* gpio = bus_getdriver("gpio_port_b");
 *   if (!gpio) while(1);
 *   ddopen(gpio);
 * 
 *   // 设置 PA0 CRL 为输出 (mode=1)
 *   ddwrite(gpio, NULL, (0 << 4) | 0x3, 1);
 * 
 *   // 置位 PB12 (mode=2)
 *   uint32_t val = 1;
 *   ddwrite(gpio, &val, 12, 2);
 * 
 *   // 批量写: 置位 mask & 复位 ~mask (mode=3)
 *   uint32_t set_mask = 0x00FF;
 *   ddwrite(gpio, &set_mask, 0xFF00, 3);
 * 
 *   // 读端口: 返回 IDR & mask (mode=1)
 *   uint32_t idr;
 *   ddread(gpio, &idr, 0x0F, 1);
 * 
 * ============================================================
 * ddwrite / ddread mode 表
 * ============================================================
 * 操作 | mode | data          | count       | 功能
 * ------+------+---------------+-------------+--------------------------
 * write |  0   | —             | —           | no-op
 * write |  1   | unused        | (pin<<4)|nib | 设置 pin 的 CR 寄存器
 * write |  2   | uint32_t* val | pin (0-15)  | BSRR 置位/复位单引脚
 * write |  3   | uint32_t* set | reset_mask  | BSRR 批量写
 * read  |  1   | uint32_t* out | bitmask     | IDR & mask 返回
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. mode=0 始终为 no-op
 * 2. 配置 CR 寄存器时 count 编码: 高 4 位=引脚号, 低 4 位=CR nibble
 * 3. gpio_port 驱动不缓存寄存器值，所有操作实时读写
 * 4. 引脚编号 0-15, CRL(0-7) / CRH(8-15) 自动选择
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

static int gpio_open(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int gpio_close(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int gpio_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    GPIO_TypeDef* gpio = (GPIO_TypeDef*)dd->dev0->private->device_register;

    switch (mode) {
    case 0:
        break;

    case 1: {
        uint32_t pin  = count >> 4;
        uint32_t nib  = count & 0xF;
        volatile uint32_t* cr = (pin < 8) ? &gpio->CRL : &gpio->CRH;
        uint32_t shift = (pin & 7) * 4;
        *cr = (*cr & ~(0xF << shift)) | ((nib & 0xF) << shift);
        break;
    }

    case 2: {
        uint32_t pin  = count & 0xF;
        uint32_t val  = data ? *(uint32_t*)data : 0;
        if (val)
            gpio->BSRR = (1 << pin);
        else
            gpio->BSRR = (1 << (pin + 16));
        (void)gpio->IDR;
        break;
    }

    case 3: {
        uint32_t set_mask   = data ? *(uint32_t*)data : 0;
        uint32_t reset_mask = count & 0xFFFF;
        gpio->BSRR = (set_mask & 0xFFFF) | ((reset_mask & 0xFFFF) << 16);
        (void)gpio->IDR;
        break;
    }
    }
    return 0;
}

static int gpio_read(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    if (mode == 1) {
        GPIO_TypeDef* gpio = (GPIO_TypeDef*)dd->dev0->private->device_register;
        uint32_t idr = gpio->IDR;
        *(uint32_t*)data = idr & count;
    }
    return 0;
}

static const pdrv_ops_t gpio_port_ops = {
    .open   = gpio_open,
    .close  = gpio_close,
    .write  = gpio_write,
    .read   = gpio_read,
};

REGISTER_DRIVER("gpio_port_a", "1\0gpioa", &gpio_port_ops, "GPIOA port");
REGISTER_DRIVER("gpio_port_b", "1\0gpiob", &gpio_port_ops, "GPIOB port");
REGISTER_DRIVER("gpio_port_c", "1\0gpioc", &gpio_port_ops, "GPIOC port");

#endif
