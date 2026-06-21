#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

static uint32_t spi_reg(dd_t* dd)
{
    return dd->dev0->private->device_register;
}

static void spi_config_gpio(dd_t* dd)
{
    uint32_t reg = spi_reg(dd);
    GPIO_TypeDef* gpio = (GPIO_TypeDef*)dd->dev1->private->device_register;

    if (reg == 0x40013000) {
        gpio->CRL = (gpio->CRL & ~(0xF << 20)) | (0xB << 20);
        gpio->CRL = (gpio->CRL & ~(0xF << 24)) | (0x4 << 24);
        gpio->CRL = (gpio->CRL & ~(0xF << 28)) | (0xB << 28);
    } else {
        gpio->CRH = (gpio->CRH & ~(0xF << 20)) | (0xB << 20);
        gpio->CRH = (gpio->CRH & ~(0xF << 24)) | (0x4 << 24);
        gpio->CRH = (gpio->CRH & ~(0xF << 28)) | (0xB << 28);
    }
}

static int spi_open(dd_t* dd)
{
    uint32_t reg = spi_reg(dd);
    SPI_TypeDef* spi = (SPI_TypeDef*)reg;

    spi_config_gpio(dd);

    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BR_0;
    spi->CR1 |= SPI_CR1_SPE;

    return 0;
}

static int spi_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    SPI_TypeDef* spi = (SPI_TypeDef*)spi_reg(dd);
    uint8_t* p = (uint8_t*)data;

    for (uint32_t i = 0; i < count; i++) {
        while (!(spi->SR & SPI_SR_TXE));
        *(volatile uint8_t*)&spi->DR = p[i];
        while (!(spi->SR & SPI_SR_RXNE));
        (void)*(volatile uint8_t*)&spi->DR;
    }
    while (spi->SR & SPI_SR_BSY);

    return (int)count;
}

static int spi_read(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    SPI_TypeDef* spi = (SPI_TypeDef*)spi_reg(dd);
    uint8_t* p = (uint8_t*)data;

    for (uint32_t i = 0; i < count; i++) {
        while (!(spi->SR & SPI_SR_TXE));
        *(volatile uint8_t*)&spi->DR = 0xFF;
        while (!(spi->SR & SPI_SR_RXNE));
        p[i] = *(volatile uint8_t*)&spi->DR;
    }
    while (spi->SR & SPI_SR_BSY);

    return (int)count;
}

static int spi_close(dd_t* dd)
{
    SPI_TypeDef* spi = (SPI_TypeDef*)spi_reg(dd);
    spi->CR1 &= ~SPI_CR1_SPE;
    return 0;
}

static const pdrv_ops_t spi_master_ops = {
    .open   = spi_open,
    .close  = spi_close,
    .write  = spi_write,
    .read   = spi_read,
};

REGISTER_DRIVER("spi_master_1", "2\0spi1\0gpioa", NULL, &spi_master_ops, "SPI1 master");
REGISTER_DRIVER("spi_master_2", "2\0spi2\0gpiob", NULL, &spi_master_ops, "SPI2 master");

#endif
