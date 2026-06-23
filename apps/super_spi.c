/**
 * @file    super_spi.c
 * @note    SPI 总线主控应用层封装 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  super_spi1 / super_spi2
 * 依赖:    spi_master_{1,2} + gpio_port_{a,b} + dma
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 在 spi_master 驱动之上封装 CS/DC/RST 引脚控制，提供
 * TFT/OLED 等 SPI 显示屏常用的命令+数据写入 API。
 * 支持轮询（mode 10/11/15）和 DMA（mode 13/16）两种传输方式。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   // 获取应用句柄（自动解析 spi_master_2 + gpio_port_b + dma）
 *   app_t* lcd = appget("super_spi2");
 *   if (!lcd) while(1);
 * 
 *   // 打开（初始化 GPIO 引脚和 SPI 外设）
 *   appopen(lcd);
 * 
 *   // 硬件复位（mode 14）
 *   appwrite(lcd, NULL, 0, 14);
 * 
 *   // 发命令字节（mode 10）
 *   uint8_t cmd = 0x11;  // SLPOUT
 *   appwrite(lcd, &cmd, 1, 10);
 * 
 *   // 发数据（mode 13 = DMA，mode 11 = 轮询）
 *   uint8_t data[] = {0x05, 0x00};
 *   appwrite(lcd, data, 2, 13);
 * 
 *   // 全屏填充
 *   uint8_t buf[1024];
 *   // ... 填充 buf 为像素色值 ...
 *   for (uint32_t n = 0; n < 240*320*2; n += sizeof(buf))
 *       appwrite(lcd, buf, sizeof(buf), 13);
 * 
 *   // 关闭
 *   appclose(lcd);
 *   appfree(lcd);
 * 
 * ============================================================
 * appwrite mode 表
 * ============================================================
 * mode | 功能                      | data        | count
 * ------+--------------------------+-------------+----------
 *  0   | no-op                    | —           | —
 *  1   | 重配置 CS/DC/RST 为输出  | NULL        | 0
 *  5   | 设置引脚号               | &uint32_t   | 1
 *      |                          | [7:0]=CS    |
 *      |                          | [15:8]=DC   |
 *      |                          | [23:16]=RST |
 * 10   | 命令写入 (DC=0)          | &cmd        | ≥1 (轮询)
 * 11   | 数据写入 (DC=1, 轮询)    | &buf        | >0
 * 12   | 设置 DMA 通道号          | &uint32_t   | 1
 * 13   | 数据写入 (DC=1, DMA)     | &buf        | >0, ≤65535
 * 14   | RST 复位序列             | NULL        | 0
 * 15   | 数据写入 (DC=0, 轮询)    | &buf        | >0
 *      | 无 DC 控制，用于无 DC 管脚的外设
 * 16   | 数据写入 (DC=0, DMA)     | &buf        | >0, ≤65535
 *      | 无 DC 控制
 * 
 * ============================================================
 * 引脚默认分配
 * ============================================================
 *         | CS  | DC  | RST | DMA通道
 * --------+-----+-----+-----+--------
 * super_spi1 | PA4 | PA2 | PA3 | DMA1_Ch3 (SPI1_TX)
 * super_spi2 | PB12| PB8 | PB9 | DMA1_Ch5 (SPI2_TX)
 * 
 * 可通过 mode=5 运行时修改 CS/DC/RST 引脚号。
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. appopen 会自动配置 SPI 时钟: SPI1→18MHz, SPI2→18MHz
 * 2. DMA 模式 (13/16) 要求 data 缓冲区在传输期间保持有效，
 *    推荐使用 static 或全局缓冲区，且 count ≤ 65535
 * 3. DMA 传输使用阻塞等待（循环查询 TCIF），传输完成后返回
 * 4. mode=0 始终为 no-op
 * 5. 本文件是 app 层（papp_t），通过 appget/appwrite 调用，
 *    不使用 dd_t/bus_getdriver 接口
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#if __USE_STM32__
#include "stm32f1xx.h"

typedef struct {
    uint8_t cs_pin;
    uint8_t dc_pin;
    uint8_t rst_pin;
    uint8_t dma_ch;
} sspi_ctx_t;

static GPIO_TypeDef* reg_gpio(app_t* app)
{
    return (GPIO_TypeDef*)app->dd1->dev0->private->device_register;
}

static SPI_TypeDef* reg_spi(app_t* app)
{
    return (SPI_TypeDef*)app->dd0->dev0->private->device_register;
}

static void gpio_cfg_out(GPIO_TypeDef* gpio, uint8_t pin)
{
    volatile uint32_t* r = (pin < 8) ? &gpio->CRL : &gpio->CRH;
    uint32_t s = (pin < 8) ? pin * 4 : (pin - 8) * 4;
    *r = (*r & ~(0xF << s)) | (0x3 << s);
}

static void pin_h(GPIO_TypeDef* gpio, uint8_t pin) { gpio->BSRR = 1 << pin; }
static void pin_l(GPIO_TypeDef* gpio, uint8_t pin) { gpio->BSRR = 1 << (pin + 16); }

static void spi_wait_txe(SPI_TypeDef* spi) { while (!(spi->SR & SPI_SR_TXE)) {} }
static void spi_wait_bsy(SPI_TypeDef* spi) { while (spi->SR & SPI_SR_BSY) {} }

static void spi_tx(SPI_TypeDef* spi, const uint8_t* buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        spi_wait_txe(spi);
        spi->DR = buf[i];
    }
    spi_wait_bsy(spi);
}

static void dma_wait_tc(uint8_t ch)
{
    DMA_TypeDef* dma = (DMA_TypeDef*)DMA1_BASE;
    uint32_t tc = 1 << ((ch - 1) * 4 + 1);
    while (!(dma->ISR & tc)) {}
    dma->IFCR = tc;
}

static int sspi_app_open(app_t* app)
{
    sspi_ctx_t* ctx = osmalloc(sizeof(sspi_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(sspi_ctx_t));
    app->app_data = ctx;

    const char* n = app->papp->base->app_name;
    if (strcmp(n, "super_spi1") == 0) {
        ctx->cs_pin  = 4;
        ctx->dc_pin  = 2;
        ctx->rst_pin = 3;
        ctx->dma_ch  = 3;  // DMA1_Channel3 = SPI1_TX
    } else {
        ctx->cs_pin  = 12;
        ctx->dc_pin  = 8;
        ctx->rst_pin = 9;
        ctx->dma_ch  = 5;  // DMA1_Channel5 = SPI2_TX
    }

    GPIO_TypeDef* gpio = reg_gpio(app);
    SPI_TypeDef* spi = reg_spi(app);
    uint32_t reg = app->dd0->dev0->private->device_register;

    gpio_cfg_out(gpio, ctx->cs_pin);
    gpio_cfg_out(gpio, ctx->dc_pin);
    gpio_cfg_out(gpio, ctx->rst_pin);
    pin_h(gpio, ctx->cs_pin);
    pin_h(gpio, ctx->dc_pin);
    pin_h(gpio, ctx->rst_pin);

    if (reg == 0x40013000) {
        gpio->CRL = (gpio->CRL & ~(0xF << 20)) | (0xB << 20);
        gpio->CRL = (gpio->CRL & ~(0xF << 24)) | (0x4 << 24);
        gpio->CRL = (gpio->CRL & ~(0xF << 28)) | (0xB << 28);
    } else {
        gpio->CRH = (gpio->CRH & ~(0xF << 20)) | (0xB << 20);
        gpio->CRH = (gpio->CRH & ~(0xF << 24)) | (0x4 << 24);
        gpio->CRH = (gpio->CRH & ~(0xF << 28)) | (0xB << 28);
    }

    if (reg == 0x40013000)
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BR_0;
    else
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;
    spi->CR1 |= SPI_CR1_SPE;

    return 0;
}

static int sspi_app_close(app_t* app)
{
    if (app->app_data) osfree(app->app_data);
    app->app_data = NULL;
    return 0;
}

static int sspi_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    GPIO_TypeDef* gpio = reg_gpio(app);
    SPI_TypeDef* spi = reg_spi(app);

    if (mode == 1 && !data) {
        gpio_cfg_out(gpio, ctx->cs_pin);
        gpio_cfg_out(gpio, ctx->dc_pin);
        gpio_cfg_out(gpio, ctx->rst_pin);
        pin_h(gpio, ctx->cs_pin);
        pin_h(gpio, ctx->dc_pin);
        pin_h(gpio, ctx->rst_pin);
        return 1;
    }

    if (mode == 5 && data && count == 1) {
        uint32_t v = *(uint32_t*)data;
        ctx->cs_pin  = v & 0xFF;
        ctx->dc_pin  = (v >> 8) & 0xFF;
        ctx->rst_pin = (v >> 16) & 0xFF;
        return 1;
    }

    if (mode == 10 && data && count >= 1) {
        pin_l(gpio, ctx->dc_pin);
        pin_l(gpio, ctx->cs_pin);
        spi_tx(spi, (uint8_t*)data, 1);
        pin_h(gpio, ctx->cs_pin);
        pin_h(gpio, ctx->dc_pin);
        return 1;
    }

    if (mode == 11 && data && count > 0) {
        pin_h(gpio, ctx->dc_pin);
        pin_l(gpio, ctx->cs_pin);
        spi_tx(spi, (uint8_t*)data, count);
        pin_h(gpio, ctx->cs_pin);
        return (int)count;
    }

    if (mode == 12 && data && count == 1) {
        ctx->dma_ch = *(uint32_t*)data;
        return 1;
    }

    if (mode == 13 && data && count > 0) {
        pin_h(gpio, ctx->dc_pin);
        pin_l(gpio, ctx->cs_pin);

        uint32_t ch = ctx->dma_ch;
        DMA_Channel_TypeDef* dch = (DMA_Channel_TypeDef*)(DMA1_BASE + 0x08 + (ch - 1) * 0x14);
        uint32_t dr = (uint32_t)(app->dd0->dev0->private->device_register + 0x0C);

        dch->CCR &= ~DMA_CCR_EN;
        dch->CPAR  = dr;
        dch->CMAR  = (uint32_t)data;
        dch->CNDTR = count;
        dch->CCR   = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;

        DMA_TypeDef* dma = (DMA_TypeDef*)DMA1_BASE;
        dma->IFCR = 1 << ((ch - 1) * 4 + 1);

        spi->CR2 |= SPI_CR2_TXDMAEN;
        dch->CCR |= DMA_CCR_EN;

        dma_wait_tc(ch);
        spi_wait_bsy(spi);

        spi->CR2 &= ~SPI_CR2_TXDMAEN;
        dch->CCR &= ~DMA_CCR_EN;

        pin_h(gpio, ctx->cs_pin);
        return (int)count;
    }

    if (mode == 15 && data && count > 0) {
        pin_l(gpio, ctx->cs_pin);
        spi_tx(spi, (uint8_t*)data, count);
        pin_h(gpio, ctx->cs_pin);
        return (int)count;
    }

    if (mode == 16 && data && count > 0) {
        pin_l(gpio, ctx->cs_pin);

        uint32_t ch = ctx->dma_ch;
        DMA_Channel_TypeDef* dch = (DMA_Channel_TypeDef*)(DMA1_BASE + 0x08 + (ch - 1) * 0x14);
        uint32_t dr = (uint32_t)(app->dd0->dev0->private->device_register + 0x0C);

        dch->CCR &= ~DMA_CCR_EN;
        dch->CPAR  = dr;
        dch->CMAR  = (uint32_t)data;
        dch->CNDTR = count;
        dch->CCR   = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;

        DMA_TypeDef* dma = (DMA_TypeDef*)DMA1_BASE;
        dma->IFCR = 1 << ((ch - 1) * 4 + 1);

        spi->CR2 |= SPI_CR2_TXDMAEN;
        dch->CCR |= DMA_CCR_EN;

        dma_wait_tc(ch);
        spi_wait_bsy(spi);

        spi->CR2 &= ~SPI_CR2_TXDMAEN;
        dch->CCR &= ~DMA_CCR_EN;

        pin_h(gpio, ctx->cs_pin);
        return (int)count;
    }

    if (mode == 14 && !data) {
        pin_l(gpio, ctx->rst_pin);
        sysdelay(100);
        pin_h(gpio, ctx->rst_pin);
        sysdelay(150);
        return 1;
    }

    return 0;
}

static int sspi_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static const papp_ops_t sspi_app_ops = {
    .open  = sspi_app_open,
    .close = sspi_app_close,
    .write = sspi_app_write,
    .read  = sspi_app_read,
};

REGISTER_APP("super_spi1", "3\0spi_master_1\0gpio_port_a\0dma",
             &sspi_app_ops, "SPI1 + GPIOA + DMA1 (TFT app)");
REGISTER_APP("super_spi2", "3\0spi_master_2\0gpio_port_b\0dma",
             &sspi_app_ops, "SPI2 + GPIOB + DMA1 (TFT app)");

#endif
