/**
 * @file    super_spi.c
 * @note    SPI 总线主控 — 多设备管理 (STM32)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  super_spi1 / super_spi2
 * 依赖:    spi_master_{1,2} + gpio_port_{a,b} + dma
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用
 * ============================================================
 *   ROM(Debug -O0):   ~2,100 B
 *   ROM(Release -Os): ~1,300 B
 *   RAM(静态):  0 B
 *   RAM(堆):    sspi_ctx_t (~28 B) + 每设备 sspi_dev_t (4 B)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("super_spi1") / appget("super_spi2") → app_t*
 *
 *   appopen(spi)     : 初始化 SPI + DMA, 不碰 GPIO
 *
 *   appioctl(spi, "reg") → int dev_id
 *     注册一个设备, 返回设备号 (0..SSPI_DEV_MAX-1)
 *
 *   appioctl(spi, "setpin", dev_id, pin_sel, gpio_pin)
 *     设置设备的某个逻辑引脚
 *     pin_sel: SSPI_CS=0, SSPI_DC=1, SSPI_R1=2, SSPI_R2=3
 *     gpio_pin: 引脚号 (0-15), 0xFF=未使用
 *
 *   appwrite(spi, data, count, SSPI_MODE(dev_id, op))
 *     见 mode 表
 *
 *   appclose(spi)    : 释放内存
 *
 * ============================================================
 * SSPI_MODE(dev_id, op) 表
 * ============================================================
 * op (低 nibble) | 功能
 * ---------------+------------------------------------------------
 * SSPI_CS_LOW    | dev 的 CS↓
 * SSPI_CS_HIGH   | dev 的 CS↑
 * SSPI_DC_LOW    | dev 的 DC↓
 * SSPI_DC_HIGH   | dev 的 DC↑
 * SSPI_R1_LOW    | dev 的 R1↓
 * SSPI_R1_HIGH   | dev 的 R1↑
 * SSPI_R2_LOW    | dev 的 R2↓
 * SSPI_R2_HIGH   | dev 的 R2↑
 * SSPI_SEND      | 裸 SPI 发送 (不管引脚, 轮询)
 * SSPI_SEND_CS   | CS↓ + 发送 + CS↑ (轮询)
 * SSPI_SEND_CMD  | DC↓ + CS↓ + 发送1字节 + CS↑ + DC↑ (轮询)
 * SSPI_SEND_DAT  | DC↑ + CS↓ + 发送 + CS↑ (轮询)
 * SSPI_SEND_DMA  | 裸 SPI DMA (不管引脚)
 * SSPI_SEND_CS_DMA  | CS↓ + DMA + CS↑
 * SSPI_SEND_DAT_DMA | DC↑ + CS↓ + DMA + CS↑
 * SSPI_PULSE_R1  | R1↓ + SYS_DELAY(100) + R1↑ + SYS_DELAY(150)
 *
 * SSPI_XFER      | 全双工收发 (spi_xfer_t*, 轮询, 不碰引脚)
 *                 | 不使用 dev_id 编码, mode = SSPI_XFER
 *
 * ============================================================
 * 典型用法
 * ============================================================
 *   app_t* spi = appget("super_spi2");
 *   appopen(spi);
 *
 *   int tft = appioctl(spi, "reg");
 *   appioctl(spi, "setpin", tft, SSPI_CS,  4);
 *   appioctl(spi, "setpin", tft, SSPI_DC,  2);
 *   appioctl(spi, "setpin", tft, SSPI_R1,  3);
 *
 *   // 复位
 *   appwrite(spi, NULL, 0, SSPI_MODE(tft, SSPI_PULSE_R1));
 *
 *   // 发命令
 *   uint8_t cmd = 0x11;
 *   appwrite(spi, &cmd, 1, SSPI_MODE(tft, SSPI_SEND_CMD));
 *
 *   // DMA 发数据
 *   uint8_t buf[1024];
 *   appwrite(spi, buf, 1024, SSPI_MODE(tft, SSPI_SEND_DAT_DMA));
 *
 *   // 关闭
 *   appclose(spi);
 *   appfree(spi);
 */

#include "../inc/app.h"
#include "../inc/super_spi.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#if __USE_STM32__
#include "stm32f1xx.h"

/* ================================================================
 * 内部数据结构
 * ================================================================ */
#define SSPI_DEV_MAX 4

typedef struct {
    uint8_t cs_pin;
    uint8_t dc_pin;
    uint8_t r1_pin;
    uint8_t r2_pin;
} sspi_dev_t;

typedef struct {
    uint8_t     dma_ch;
    uint8_t     dev_count;
    sspi_dev_t  dev[SSPI_DEV_MAX];
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

static void pincfg(GPIO_TypeDef* gpio, uint8_t pin)
{
    if (pin != SSPI_PIN_NONE) gpio_cfg_out(gpio, pin);
}
static void pinset_h(GPIO_TypeDef* gpio, uint8_t pin)
{
    if (pin != SSPI_PIN_NONE) pin_h(gpio, pin);
}
static void pinset_l(GPIO_TypeDef* gpio, uint8_t pin)
{
    if (pin != SSPI_PIN_NONE) pin_l(gpio, pin);
}

static void spi_wait_txe(SPI_TypeDef* spi) { while (!(spi->SR & SPI_SR_TXE)) {} }
static void spi_wait_bsy(SPI_TypeDef* spi) { while (spi->SR & SPI_SR_BSY) {} }

static void spi_tx(SPI_TypeDef* spi, const uint8_t* buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        spi_wait_txe(spi);
        spi->DR = buf[i];
        while (!(spi->SR & SPI_SR_RXNE));
        (void)spi->DR;
    }
    spi_wait_bsy(spi);
}

typedef struct {
    void*   tx_buf;
    uint16_t tx_len;
    void*   rx_buf;
    uint16_t rx_len;
} spi_xfer_t;

static void dma_wait_tc(uint8_t ch)
{
    DMA_TypeDef* dma = (DMA_TypeDef*)DMA1_BASE;
    uint32_t tc = 1 << ((ch - 1) * 4 + 1);
    while (!(dma->ISR & tc)) {}
    dma->IFCR = tc;
}

static void dma_send(DMA_TypeDef* dma, SPI_TypeDef* spi, uint8_t ch,
    const uint8_t* data, uint16_t count)
{
    DMA_Channel_TypeDef* dch = (DMA_Channel_TypeDef*)(DMA1_BASE + 0x08 + (ch - 1) * 0x14);
    uint32_t dr = (uint32_t)(&spi->DR);

    dch->CCR &= ~DMA_CCR_EN;
    dch->CPAR  = dr;
    dch->CMAR  = (uint32_t)data;
    dch->CNDTR = count;
    dch->CCR   = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;

    dma->IFCR = 1 << ((ch - 1) * 4 + 1);
    spi->CR2 |= SPI_CR2_TXDMAEN;
    dch->CCR |= DMA_CCR_EN;

    dma_wait_tc(ch);
    spi_wait_bsy(spi);

    spi->CR2 &= ~SPI_CR2_TXDMAEN;
    dch->CCR &= ~DMA_CCR_EN;
}

/* ================================================================
 * 设备表访问
 * ================================================================ */
static sspi_dev_t* get_dev(sspi_ctx_t* ctx, uint32_t mode)
{
    uint8_t dev_id = (mode >> 4) & 0x0F;
    if (dev_id >= ctx->dev_count) return NULL;
    return &ctx->dev[dev_id];
}

/* ================================================================
 * App lifecycle
 * ================================================================ */
static int sspi_app_open(app_t* app)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)osmalloc(sizeof(sspi_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(sspi_ctx_t));

    const char* n = app->papp->base->app_name;
    if (strcmp(n, "super_spi1") == 0)
        ctx->dma_ch = 3;
    else
        ctx->dma_ch = 5;

    app->app_data = ctx;

    GPIO_TypeDef* gpio = reg_gpio(app);
    SPI_TypeDef* spi = reg_spi(app);
    uint32_t reg = app->dd0->dev0->private->device_register;

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

/* ================================================================
 * ioctl
 * ================================================================ */
static int sspi_app_ioctl(app_t* app, const char* fmt, va_list ap)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    if (!ctx) return -1;

    /* reg — 注册设备 */
    if (strcmp(fmt, "reg") == 0) {
        if (ctx->dev_count >= SSPI_DEV_MAX) return -1;
        int id = ctx->dev_count++;
        ctx->dev[id].cs_pin = SSPI_PIN_NONE;
        ctx->dev[id].dc_pin = SSPI_PIN_NONE;
        ctx->dev[id].r1_pin = SSPI_PIN_NONE;
        ctx->dev[id].r2_pin = SSPI_PIN_NONE;
        return id;
    }

    /* setpin — 设置设备的逻辑引脚 */
    if (strcmp(fmt, "setpin") == 0) {
        int dev_id = va_arg(ap, int);
        int sel    = va_arg(ap, int);
        int pin    = va_arg(ap, int);
        if (dev_id < 0 || dev_id >= (int)ctx->dev_count) return -1;

        GPIO_TypeDef* gpio = reg_gpio(app);
        uint8_t* p;
        switch (sel) {
            case SSPI_CS: p = &ctx->dev[dev_id].cs_pin; break;
            case SSPI_DC: p = &ctx->dev[dev_id].dc_pin; break;
            case SSPI_R1: p = &ctx->dev[dev_id].r1_pin; break;
            case SSPI_R2: p = &ctx->dev[dev_id].r2_pin; break;
            default: return -1;
        }
        *p = (uint8_t)pin;
        pincfg(gpio, (uint8_t)pin);
        pinset_h(gpio, (uint8_t)pin);
        return 1;
    }

    return 0;
}

/* ================================================================
 * appwrite — 引脚操作 + SPI 发送
 * ================================================================ */
static int sspi_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    GPIO_TypeDef* gpio = reg_gpio(app);
    SPI_TypeDef* spi = reg_spi(app);

    /* SSPI_XFER — 全双工收发 (不依赖 dev_id) */
    if (mode == SSPI_XFER && data && count == 1) {
        spi_xfer_t* x = (spi_xfer_t*)data;
        uint32_t n = x->tx_len + x->rx_len;
        if (!n) return 0;
        for (uint32_t i = 0; i < n; i++) {
            spi_wait_txe(spi);
            uint8_t txb = (i < x->tx_len) ? ((uint8_t*)x->tx_buf)[i] : 0xFF;
            spi->DR = txb;
            while (!(spi->SR & SPI_SR_RXNE));
            uint8_t rxb = spi->DR;
            if (i >= x->tx_len && x->rx_buf)
                ((uint8_t*)x->rx_buf)[i - x->tx_len] = rxb;
        }
        spi_wait_bsy(spi);
        return 1;
    }

    /* 以下操作需要 dev_id */
    sspi_dev_t* d = get_dev(ctx, mode);
    if (!d) return 0;
    uint8_t op = mode & 0x0F;

    switch (op) {

    /* --- 纯引脚操作 --- */
    case SSPI_CS_LOW:
        pinset_l(gpio, d->cs_pin);
        return 1;
    case SSPI_CS_HIGH:
        pinset_h(gpio, d->cs_pin);
        return 1;
    case SSPI_DC_LOW:
        pinset_l(gpio, d->dc_pin);
        return 1;
    case SSPI_DC_HIGH:
        pinset_h(gpio, d->dc_pin);
        return 1;
    case SSPI_R1_LOW:
        pinset_l(gpio, d->r1_pin);
        return 1;
    case SSPI_R1_HIGH:
        pinset_h(gpio, d->r1_pin);
        return 1;
    case SSPI_R2_LOW:
        pinset_l(gpio, d->r2_pin);
        return 1;
    case SSPI_R2_HIGH:
        pinset_h(gpio, d->r2_pin);
        return 1;

    /* --- SPI 发送 (轮询) --- */
    case SSPI_SEND:
        if (!data || count == 0) return 0;
        spi_tx(spi, (uint8_t*)data, count);
        return (int)count;

    case SSPI_SEND_CS:
        if (!data || count == 0) return 0;
        pinset_l(gpio, d->cs_pin);
        spi_tx(spi, (uint8_t*)data, count);
        pinset_h(gpio, d->cs_pin);
        return (int)count;

    case SSPI_SEND_CMD:
        if (!data || count < 1) return 0;
        pinset_l(gpio, d->dc_pin);
        pinset_l(gpio, d->cs_pin);
        spi_tx(spi, (uint8_t*)data, 1);
        pinset_h(gpio, d->cs_pin);
        pinset_h(gpio, d->dc_pin);
        return 1;

    case SSPI_SEND_DAT:
        if (!data || count == 0) return 0;
        pinset_h(gpio, d->dc_pin);
        pinset_l(gpio, d->cs_pin);
        spi_tx(spi, (uint8_t*)data, count);
        pinset_h(gpio, d->cs_pin);
        return (int)count;

    /* --- SPI 发送 (DMA) --- */
    case SSPI_SEND_DMA:
        if (!data || count == 0) return 0;
        dma_send((DMA_TypeDef*)DMA1_BASE, spi, ctx->dma_ch, (uint8_t*)data, (uint16_t)count);
        return (int)count;

    case SSPI_SEND_CS_DMA:
        if (!data || count == 0) return 0;
        pinset_l(gpio, d->cs_pin);
        dma_send((DMA_TypeDef*)DMA1_BASE, spi, ctx->dma_ch, (uint8_t*)data, (uint16_t)count);
        pinset_h(gpio, d->cs_pin);
        return (int)count;

    case SSPI_SEND_DAT_DMA:
        if (!data || count == 0) return 0;
        pinset_h(gpio, d->dc_pin);
        pinset_l(gpio, d->cs_pin);
        dma_send((DMA_TypeDef*)DMA1_BASE, spi, ctx->dma_ch, (uint8_t*)data, (uint16_t)count);
        pinset_h(gpio, d->cs_pin);
        return (int)count;

    /* --- 便捷操作 --- */
    case SSPI_PULSE_R1:
        pinset_l(gpio, d->r1_pin);
        sysdelay(100);
        pinset_h(gpio, d->r1_pin);
        sysdelay(150);
        return 1;

    default:
        return 0;
    }
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
    .ioctl = sspi_app_ioctl,
};

REGISTER_APP("super_spi1", "3\0spi_master_1\0gpio_port_a\0dma",
             &sspi_app_ops, "SPI1 + GPIOA + DMA1 (SPI bus master)");
REGISTER_APP("super_spi2", "3\0spi_master_2\0gpio_port_b\0dma",
             &sspi_app_ops, "SPI2 + GPIOB + DMA1 (SPI bus master)");

#endif
