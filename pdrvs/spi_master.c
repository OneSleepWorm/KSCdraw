#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

typedef struct {
    SPI_TypeDef* spi;
    uint8_t      inst_no;
} spi_ctx_t;

static int spi_probe(dd_t* dd)
{
    spi_ctx_t* ctx = (spi_ctx_t*)osmalloc(sizeof(spi_ctx_t));
    if (!ctx) return -1;
    ctx->spi = (SPI_TypeDef*)dd->dev->private->device_register;
    ctx->inst_no = dd->dev->private->inst_no;
    dd->driver_data = ctx;
    return 0;
}

static int spi_remove(dd_t* dd)
{
    if (dd->driver_data) {
        osfree(dd->driver_data);
        dd->driver_data = NULL;
    }
    return 0;
}

static void spi_enable_rcc(uint8_t inst_no)
{
    switch (inst_no) {
        case 1: RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_IOPAEN; break;
        case 2: RCC->APB1ENR |= RCC_APB1ENR_SPI2EN; RCC->APB2ENR |= RCC_APB2ENR_IOPBEN; break;
    }
    (void)RCC->APB2ENR;
}

static void spi_config_gpio(uint8_t inst_no)
{
    if (inst_no == 1) {
        GPIOA->CRL = (GPIOA->CRL & ~(0xFFF << 20)) | (0xB << 20) | (0x4 << 24) | (0xB << 28);
    } else if (inst_no == 2) {
        GPIOB->CRH = (GPIOB->CRH & ~(0xFFF << 4)) | (0xB << 4) | (0x4 << 8) | (0xB << 12);
    }
}

static int spi_open(dd_t* dd)
{
    spi_ctx_t* ctx = (spi_ctx_t*)dd->driver_data;

    spi_enable_rcc(ctx->inst_no);
    spi_config_gpio(ctx->inst_no);

    ctx->spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BR_0;
    ctx->spi->CR1 |= SPI_CR1_SPE;

    return 0;
}

static int spi_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    spi_ctx_t* ctx = (spi_ctx_t*)dd->driver_data;
    uint8_t* p = (uint8_t*)data;

    for (uint32_t i = 0; i < count; i++) {
        while (!(ctx->spi->SR & SPI_SR_TXE));
        *(volatile uint8_t*)&ctx->spi->DR = p[i];
        while (!(ctx->spi->SR & SPI_SR_RXNE));
        (void)*(volatile uint8_t*)&ctx->spi->DR;
    }
    while (ctx->spi->SR & SPI_SR_BSY);

    return (int)count;
}

static int spi_read(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    spi_ctx_t* ctx = (spi_ctx_t*)dd->driver_data;
    uint8_t* p = (uint8_t*)data;

    for (uint32_t i = 0; i < count; i++) {
        while (!(ctx->spi->SR & SPI_SR_TXE));
        *(volatile uint8_t*)&ctx->spi->DR = 0xFF;
        while (!(ctx->spi->SR & SPI_SR_RXNE));
        p[i] = *(volatile uint8_t*)&ctx->spi->DR;
    }
    while (ctx->spi->SR & SPI_SR_BSY);

    return (int)count;
}

static int spi_close(dd_t* dd)
{
    spi_ctx_t* ctx = (spi_ctx_t*)dd->driver_data;
    if (!ctx) return -1;
    ctx->spi->CR1 &= ~SPI_CR1_SPE;
    return 0;
}

static const pdrv_sysfunc_t spi_sysfunc = {
    .probe = spi_probe,
    .remove = spi_remove,
};

static const pdrv_ops_t spi_master_ops = {
    .ops_name = "master",
    .open   = spi_open,
    .close  = spi_close,
    .write  = spi_write,
    .read   = spi_read,
};

REGISTER_DRIVER("spi_master", &spi_sysfunc, &spi_master_ops, "SPI master");

#endif
