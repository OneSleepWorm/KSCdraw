/**
 * @file    spi_master.c
 * @note    SPI 主控底层驱动 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  spi_master_1 / spi_master_2
 * 依赖:    spi1+gpioa / spi2+gpiob
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 提供寄存器级 SPI 主控读写操作，不包含 CS/DC 等控制信号。
 * 适用于需要直接访问 SPI 总线的场景。支持全双工轮询传输。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   dd_t* spi = bus_getdriver("spi_master_2");
 *   if (!spi) while(1);
 *   ddopen(spi);  // 配置 GPIO + SPI CR1, 使能 SPE
 * 
 *   uint8_t tx[] = {0xAA, 0xBB, 0xCC};
 *   ddwrite(spi, tx, 3, 0);  // 发送 3 字节
 * 
 *   uint8_t rx[3];
 *   ddread(spi, rx, 3, 0);   // 接收 3 字节 (发送 0xFF)
 * 
 *   ddclose(spi);
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. SPI1 (APB2=72MHz) → 18MHz；SPI2 (APB1=36MHz) → 18MHz
 * 2. write 等待 RXNE 并丢弃收到的字节（全双工必须读 DR）
 * 3. read 发送 0xFF 作为时钟，将 MISO 数据读到缓冲区
 * 4. 本驱动不管理 CS/DC 引脚，需外部控制
 * 5. 使用软件 NSS (SSM+SSI)，无需硬件 NSS 引脚
 * 6. mode 参数被忽略 (始终执行)
 */

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

    if (reg == 0x40013000)
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BR_0;
    else
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;
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

REGISTER_DRIVER("spi_master_1", "2\0spi1\0gpioa", &spi_master_ops, "SPI1 master");
REGISTER_DRIVER("spi_master_2", "2\0spi2\0gpiob", &spi_master_ops, "SPI2 master");

#endif
