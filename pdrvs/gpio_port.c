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
        break;
    }

    case 3: {
        uint32_t set_mask   = data ? *(uint32_t*)data : 0;
        uint32_t reset_mask = count & 0xFFFF;
        gpio->BSRR = (set_mask & 0xFFFF) | ((reset_mask & 0xFFFF) << 16);
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

REGISTER_DRIVER("gpio_port_a", "1\0gpioa", NULL, &gpio_port_ops, "GPIOA port");
REGISTER_DRIVER("gpio_port_b", "1\0gpiob", NULL, &gpio_port_ops, "GPIOB port");
REGISTER_DRIVER("gpio_port_c", "1\0gpioc", NULL, &gpio_port_ops, "GPIOC port");

#endif
