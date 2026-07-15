/**
 * @file    super_spi.c
 * @note    SPI 总线主控 — 统一 SPI1+SPI2, 无 pdrv 依赖 (STM32)
 * @flash   ~2672B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  super_spi
 * app_dep: gpio_port (app 层依赖)
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用 (LTO差分法)
 * ============================================================
 *   ROM(Debug -O0):   2,980 B
 *   ROM(Release -Os):  1,372 B
 *   RAM(静态):   0 B
 *   RAM(堆):     ~100 B (sspi_ctx_t + dev slots, osmalloc)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("super_spi") → app_t*
 *
 *   appopen(spi)    : 使能 RCC, 初始化 DMA 状态
 *
 *   appcmd(spi, "reg -i <inst>") → int dev_id
 *     inst=1(SPI1) / 2(SPI2)
 *     注册一个设备, 返回设备号 (0..SSPI_DEV_MAX-1, 每实例独立)
 *
 *   sspi_setpin(spi, inst, dev_id, sel, pin)
 *     (或 appcmd(spi, "setpin -i <inst> -d <dev_id> -s <sel> -p <pin>"))
 *     设置设备的某个逻辑引脚 (走 gpio_port app)
 *     sel: SSPI_CS=0, SSPI_DC=1, SSPI_R1=2, SSPI_R2=3
 *     pin: 引脚号 (0-15, 内部自动加 gpio_port 偏移)
 *
 *   appwrite(spi, data, count, SSPI_MODE(spi_inst, dev_id, op))
 *     见 mode 表
 *
 *   appclose(spi)  : 释放内存
 *
 * ============================================================
 * SSPI_MODE(spi_inst, dev_id, op) 表
 * ============================================================
 *  op (低 nibble) | 功能
 *  ---------------+------------------------------------------------
 *  SSPI_CS_LOW    | dev 的 CS↓
 *  SSPI_CS_HIGH   | dev 的 CS↑
 *  SSPI_DC_LOW    | dev 的 DC↓
 *  SSPI_DC_HIGH   | dev 的 DC↑
 *  SSPI_R1_LOW    | dev 的 R1↓
 *  SSPI_R1_HIGH   | dev 的 R1↑
 *  SSPI_R2_LOW    | dev 的 R2↓
 *  SSPI_R2_HIGH   | dev 的 R2↑
 *  SSPI_SEND      | 裸 SPI 发送 (不管引脚, 轮询)
 *  SSPI_SEND_CS   | CS↓ + 发送 + CS↑ (轮询)
 *  SSPI_SEND_CMD  | DC↓ + CS↓ + 发送1字节 + CS↑ + DC↑ (轮询)
 *  SSPI_SEND_DAT  | DC↑ + CS↓ + 发送 + CS↑ (轮询)
 *  SSPI_SEND_DMA  | 裸 SPI DMA (不管引脚)
 *  SSPI_SEND_CS_DMA  | CS↓ + DMA + CS↑
 *  SSPI_SEND_DAT_DMA | DC↑ + CS↓ + DMA + CS↑
 *  SSPI_PULSE_R1  | R1↓ + SYS_DELAY(100) + R1↑ + SYS_DELAY(150)
 *
 *  SSPI_XFER / SSPI_XFER_INST(i) | 全双工收发 (spi_xfer_t*, 轮询, 不碰引脚)
 *
 * ============================================================
 * 典型用法
 * ============================================================
 *   app_t* spi = appget("super_spi");
 *   appopen(spi);
 *
 *   int tft1 = appcmd(spi, "reg -i 1");  // SPI1
 *   sspi_setpin(spi, 1, tft1, SSPI_CS,  4);
 *   sspi_setpin(spi, 1, tft1, SSPI_DC,  2);
 *   sspi_setpin(spi, 1, tft1, SSPI_R1,  3);
 *
 *   appwrite(spi, NULL, 0, SSPI_MODE(1, tft1, SSPI_PULSE_R1));
 *
 *   uint8_t cmd = 0x11;
 *   appwrite(spi, &cmd, 1, SSPI_MODE(1, tft1, SSPI_SEND_CMD));
 *
 *   uint8_t buf[1024];
 *   appwrite(spi, buf, 1024, SSPI_MODE(1, tft1, SSPI_SEND_DAT_DMA));
 *
 *   int tft2 = appcmd(spi, "reg -i 2");  // SPI2
 *   sspi_setpin(spi, 2, tft2, SSPI_CS, 12);
 *   appwrite(spi, buf, 1024, SSPI_MODE(2, tft2, SSPI_SEND_DAT_DMA));
 *
 *   appclose(spi);
 *   appfree(spi);
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "app_config.h"
#include <string.h>
#include "stm32f1xx.h"

#define SSPI_DEV_MAX 4

typedef struct {
    uint8_t cs_pin;
    uint8_t dc_pin;
    uint8_t r1_pin;
    uint8_t r2_pin;
} sspi_dev_t;

typedef struct {
    SPI_TypeDef*  spi;
    uint32_t      gpio_base;
    uint8_t       dma_ch;
    uint8_t       inited;
    uint8_t       br;
} sspi_inst_t;

typedef struct {
    sspi_inst_t inst[2];
    uint8_t     dev_count[2];
    sspi_dev_t  dev[2][SSPI_DEV_MAX];
} sspi_ctx_t;

static void spi_wait_txe(SPI_TypeDef* spi)
{
    while (!(spi->SR & SPI_SR_TXE)) {}
}

static void spi_wait_bsy(SPI_TypeDef* spi)
{
    while (spi->SR & SPI_SR_BSY) {}
}

static void spi_tx(SPI_TypeDef* spi, const uint8_t* buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        spi_wait_txe(spi);
        spi->DR = buf[i];
        while (!(spi->SR & SPI_SR_RXNE)) {}
        (void)spi->DR;
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

static void dma_send(SPI_TypeDef* spi, uint8_t ch,
    const uint8_t* data, uint16_t count)
{
    DMA_TypeDef* dma = (DMA_TypeDef*)DMA1_BASE;
    DMA_Channel_TypeDef* dch =
        (DMA_Channel_TypeDef*)(DMA1_BASE + 0x08 + (ch - 1) * 0x14);
    uint32_t dr = (uint32_t)(&spi->DR);

    dch->CCR &= ~DMA_CCR_EN;
    dch->CPAR  = dr;
    dch->CMAR  = (uint32_t)data;
    dch->CNDTR = count;
    dch->CCR   = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;

    dma->IFCR = 1 << ((ch - 1) * 4 + 1);
    dch->CCR |= DMA_CCR_EN;
    spi->CR2 |= SPI_CR2_TXDMAEN;

    dma_wait_tc(ch);
    spi_wait_bsy(spi);

    spi->CR2 &= ~SPI_CR2_TXDMAEN;
    dch->CCR &= ~DMA_CCR_EN;
    (void)spi->DR;
    (void)spi->SR;
}

static void lazy_init_spi(uint8_t idx, sspi_ctx_t* ctx)
{
    if (ctx->inst[idx].inited) return;
    ctx->inst[idx].inited = 1;

    SPI_TypeDef* spi = ctx->inst[idx].spi;

    uint8_t br = ctx->inst[idx].br & 7;
    if (idx == 0) {
        GPIOA->CRL = (GPIOA->CRL & ~(0xF << 20)) | (0xB << 20);
        GPIOA->CRL = (GPIOA->CRL & ~(0xF << 24)) | (0x4 << 24);
        GPIOA->CRL = (GPIOA->CRL & ~(0xF << 28)) | (0xB << 28);
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | (br << 3);
    } else {
        GPIOB->CRH = (GPIOB->CRH & ~(0xF << 20)) | (0xB << 20);
        GPIOB->CRH = (GPIOB->CRH & ~(0xF << 24)) | (0x4 << 24);
        GPIOB->CRH = (GPIOB->CRH & ~(0xF << 28)) | (0xB << 28);
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | (br << 3);
    }
    spi->CR1 |= SPI_CR1_SPE;
}

static void gpio_cfg_out(app_t* gpio_app, uint32_t abs_pin)
{
    appwrite(gpio_app, NULL, (abs_pin << 4) | 0x3, 1);
}

static void gpio_set(app_t* gpio_app, uint32_t abs_pin, uint32_t val)
{
    appwrite(gpio_app, &val, abs_pin, 2);
}

static sspi_dev_t* get_dev(sspi_ctx_t* ctx, uint32_t mode, uint8_t* out_idx)
{
    uint8_t idx = (mode >> 6) & 1;
    uint8_t dev_id = (mode >> 4) & 3;
    if (out_idx) *out_idx = idx;
    if (dev_id >= ctx->dev_count[idx]) return NULL;
    return &ctx->dev[idx][dev_id];
}

static int sspi_app_open(app_t* app)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)osmalloc(sizeof(sspi_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(sspi_ctx_t));

    ctx->inst[0].spi       = SPI1;
    ctx->inst[0].gpio_base = 0;
    ctx->inst[0].dma_ch    = 3;
    ctx->inst[0].br        = 1;

    ctx->inst[1].spi       = SPI2;
    ctx->inst[1].gpio_base = 16;
    ctx->inst[1].dma_ch    = 5;
    ctx->inst[1].br        = 1;

    app->app_data = ctx;

    if (app->app0) appopen(app->app0);

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
    (void)RCC->APB2ENR;

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
    if (!ctx) return 0;

    if (mode & 0x80) {
        uint8_t xfer_idx = (mode >> 6) & 1;
        if (data && count == 1) {
            spi_xfer_t* x = (spi_xfer_t*)data;
            uint32_t n = x->tx_len + x->rx_len;
            if (!n) return 0;
            lazy_init_spi(xfer_idx, ctx);
            SPI_TypeDef* spi = ctx->inst[xfer_idx].spi;
            for (uint32_t i = 0; i < n; i++) {
                spi_wait_txe(spi);
                uint8_t txb = (i < x->tx_len) ? ((uint8_t*)x->tx_buf)[i] : 0xFF;
                spi->DR = txb;
                while (!(spi->SR & SPI_SR_RXNE)) {}
                uint8_t rxb = spi->DR;
                if (i >= x->tx_len && x->rx_buf)
                    ((uint8_t*)x->rx_buf)[i - x->tx_len] = rxb;
            }
            spi_wait_bsy(spi);
        }
        return 1;
    }

    uint8_t idx;
    sspi_dev_t* d = get_dev(ctx, mode, &idx);
    if (!d) return 0;
    uint8_t op = mode & 0x0F;
    lazy_init_spi(idx, ctx);
    SPI_TypeDef* spi = ctx->inst[idx].spi;
    app_t* gpio = app->app0;
    uint32_t gb = ctx->inst[idx].gpio_base;

    switch (op) {

    case SSPI_CS_LOW:
        gpio_set(gpio, gb + d->cs_pin, 0);
        return 1;
    case SSPI_CS_HIGH:
        gpio_set(gpio, gb + d->cs_pin, 1);
        return 1;
    case SSPI_DC_LOW:
        gpio_set(gpio, gb + d->dc_pin, 0);
        return 1;
    case SSPI_DC_HIGH:
        gpio_set(gpio, gb + d->dc_pin, 1);
        return 1;
    case SSPI_R1_LOW:
        gpio_set(gpio, gb + d->r1_pin, 0);
        return 1;
    case SSPI_R1_HIGH:
        gpio_set(gpio, gb + d->r1_pin, 1);
        return 1;
    case SSPI_R2_LOW:
        gpio_set(gpio, gb + d->r2_pin, 0);
        return 1;
    case SSPI_R2_HIGH:
        gpio_set(gpio, gb + d->r2_pin, 1);
        return 1;

    case SSPI_SEND:
        if (!data || count == 0) return 0;
        spi_tx(spi, (uint8_t*)data, count);
        return (int)count;

    case SSPI_SEND_CS:
        if (!data || count == 0) return 0;
        gpio_set(gpio, gb + d->cs_pin, 0);
        spi_tx(spi, (uint8_t*)data, count);
        gpio_set(gpio, gb + d->cs_pin, 1);
        return (int)count;

    case SSPI_SEND_CMD:
        if (!data || count < 1) return 0;
        gpio_set(gpio, gb + d->dc_pin, 0);
        gpio_set(gpio, gb + d->cs_pin, 0);
        spi_tx(spi, (uint8_t*)data, (uint16_t)count);
        gpio_set(gpio, gb + d->cs_pin, 1);
        gpio_set(gpio, gb + d->dc_pin, 1);
        return 1;

    case SSPI_SEND_DAT:
        if (!data || count < 1) return 0;
        gpio_set(gpio, gb + d->dc_pin, 1);
        gpio_set(gpio, gb + d->cs_pin, 0);
        spi_tx(spi, (uint8_t*)data, (uint16_t)count);
        gpio_set(gpio, gb + d->cs_pin, 1);
        return 1;

    case SSPI_SEND_DMA:
        if (!data || count == 0) return 0;
        dma_send(spi, ctx->inst[idx].dma_ch, (uint8_t*)data, (uint16_t)count);
        return (int)count;

    case SSPI_SEND_CS_DMA:
        if (!data || count == 0) return 0;
        gpio_set(gpio, gb + d->cs_pin, 0);
        dma_send(spi, ctx->inst[idx].dma_ch, (uint8_t*)data, (uint16_t)count);
        gpio_set(gpio, gb + d->cs_pin, 1);
        return (int)count;

    case SSPI_SEND_DAT_DMA:
        if (!data || count == 0) return 0;
        gpio_set(gpio, gb + d->dc_pin, 1);
        gpio_set(gpio, gb + d->cs_pin, 0);
        dma_send(spi, ctx->inst[idx].dma_ch, (uint8_t*)data, (uint16_t)count);
        gpio_set(gpio, gb + d->cs_pin, 1);
        return (int)count;

    case SSPI_PULSE_R1:
        gpio_set(gpio, gb + d->r1_pin, 0);
        sysdelay(100);
        gpio_set(gpio, gb + d->r1_pin, 1);
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

/* ================================================================
 * appcmd handlers — 通过 appcmd(sspi, "init -i 2") 调用
 *
 * 参数规则:
 *   -i <inst> : SPI 实例 (1=SPI1, 2=SPI2)
 *   -b <0..7> : SPI 波特率分频 (init 用, BR[2:0], 默认 1)
 *   -n <count> : 收发字节数 (tx 用)
 *   -m         : 使用 mode_data.len 作为字节数 (tx 用)
 *
 * 典型用法:
 *   appcmd(sspi, "init -i 2");             // 初始化 SPI2 (默认 BR=1, 分频/4)
 *   appcmd(sspi, "init -i 2 -b 0");        // 初始化 SPI2, BR=0 (分频/2, 18MHz)
 *   appcmd(sspi, "tx -i 2 -n 1");          // SPI2 收发 1 字节
 *   sspi->user_data = buf;
 *   appcmd(sspi, "tx -i 2 -m");            // 使用 mode_data 长度
 *
 * 注意: tx 的发送数据来自 app->user_data, 接收数据存 app->callback_data
 * ================================================================ */

/* init: 初始化 SPI 实例 — -i inst */
static int cmd_init(app_t* app, const char** argv)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    int inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 2) return -1;
    uint8_t idx = (uint8_t)(inst - 1);
    if (APPCMD_HAS(argv, 'b')) {
        int b = strtoul(argv[APPCMD_ARG('b')], NULL, 0);
        if (b < 0 || b > 7) return -1;
        ctx->inst[idx].br = (uint8_t)b;
    }
    lazy_init_spi(idx, ctx);
    return 1;
}

static int cmd_tx(app_t* app, const char** argv)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    if (!APPCMD_HAS(argv, 'n') && !(APPCMD_HAS(argv, 'm') && app->mode_data)) return -1;
    int inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 2) return -1;

    int n;
    if (APPCMD_HAS(argv, 'n')) {
        n = strtoul(argv[APPCMD_ARG('n')], NULL, 0);
        if (n < 1 || n > 65535) return -1;
    } else {
        n = ((sspi_mode_t*)app->mode_data)->len;
        if (n < 1 || n > 65535) return -1;
    }

    uint8_t idx = (uint8_t)(inst - 1);
    lazy_init_spi(idx, ctx);
    SPI_TypeDef* spi = ctx->inst[idx].spi;

    uint8_t* txb = (uint8_t*)app->user_data;
    uint8_t* rxb = (uint8_t*)app->callback_data;

    for (int i = 0; i < n; i++) {
        spi_wait_txe(spi);
        spi->DR = txb ? txb[i] : 0xFF;
        while (!(spi->SR & SPI_SR_RXNE)) {}
        uint8_t r = spi->DR;
        if (rxb) rxb[i] = r;
    }
    spi_wait_bsy(spi);
    return n;
}

/* reg: 注册设备 — -i inst */
static int cmd_reg(app_t* app, const char** argv)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    int inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > 2) return -1;
    uint8_t idx = (uint8_t)(inst - 1);
    if (ctx->dev_count[idx] >= SSPI_DEV_MAX) return -1;
    int id = ctx->dev_count[idx]++;
    ctx->dev[idx][id].cs_pin = SSPI_PIN_NONE;
    ctx->dev[idx][id].dc_pin = SSPI_PIN_NONE;
    ctx->dev[idx][id].r1_pin = SSPI_PIN_NONE;
    ctx->dev[idx][id].r2_pin = SSPI_PIN_NONE;
    return id;
}

/* setpin: 设置设备引脚 — -i inst -d dev_id -s sel -p pin */
static int cmd_setpin(app_t* app, const char** argv)
{
    sspi_ctx_t* ctx = (sspi_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (!APPCMD_HAS(argv, 'i') || !APPCMD_HAS(argv, 'd') ||
        !APPCMD_HAS(argv, 's') || !APPCMD_HAS(argv, 'p')) return -1;
    int inst = strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    int dev_id = strtoul(argv[APPCMD_ARG('d')], NULL, 0);
    int sel = strtoul(argv[APPCMD_ARG('s')], NULL, 0);
    int pin = strtoul(argv[APPCMD_ARG('p')], NULL, 0);
    if (inst < 1 || inst > 2) return -1;
    uint8_t idx = (uint8_t)(inst - 1);
    if (dev_id < 0 || dev_id >= (int)ctx->dev_count[idx]) return -1;

    uint8_t* p;
    switch (sel) {
        case SSPI_CS: p = &ctx->dev[idx][dev_id].cs_pin; break;
        case SSPI_DC: p = &ctx->dev[idx][dev_id].dc_pin; break;
        case SSPI_R1: p = &ctx->dev[idx][dev_id].r1_pin; break;
        case SSPI_R2: p = &ctx->dev[idx][dev_id].r2_pin; break;
        default: return -1;
    }
    *p = (uint8_t)pin;
    uint32_t abs_pin = ctx->inst[idx].gpio_base + (uint8_t)pin;
    gpio_cfg_out(app->app0, abs_pin);
    gpio_set(app->app0, abs_pin, 1);
    return 1;
}

/* sspi_setpin: 便捷封装 - 手动组装命令字符串，避免 sprintf */
int sspi_setpin(app_t* sspi, int inst, int dev_id, int sel, int pin)
{
    char b[40];
    int n = 0;
    const char* p = "setpin -i ";
    while (*p) b[n++] = *p++;
    b[n++] = '0' + inst;
    p = " -d ";
    while (*p) b[n++] = *p++;
    if (dev_id >= 10) b[n++] = '0' + (dev_id / 10);
    b[n++] = '0' + (dev_id % 10);
    p = " -s ";
    while (*p) b[n++] = *p++;
    if (sel >= 10) b[n++] = '0' + (sel / 10);
    b[n++] = '0' + (sel % 10);
    p = " -p ";
    while (*p) b[n++] = *p++;
    if (pin >= 10) b[n++] = '0' + (pin / 10);
    b[n++] = '0' + (pin % 10);
    b[n] = '\0';
    return appcmd(sspi, b);
}

/* appcmd dispatch table — 命令名 → handler */
typedef struct { const char* name; int (*handler)(app_t*, const char**); } sspi_cmd_t;

static const sspi_cmd_t sspi_cmds[] = {
    {"init", cmd_init}, /* 初始化 SPI 实例 -i inst [-b br] */
    {"tx",   cmd_tx},   /* SPI 全双工收发  -i inst -n <count> | -m */
    {"reg",  cmd_reg},  /* 注册设备 -i inst */
    {"setpin", cmd_setpin}, /* 设置引脚 -i inst -d dev_id -s sel -p pin */
    {NULL, NULL}
};

static int sspi_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const sspi_cmd_t* e = sspi_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t sspi_app_ops = {
    .open  = sspi_app_open,
    .close = sspi_app_close,
    .write = sspi_app_write,
    .read  = sspi_app_read,
    .cmd   = sspi_app_cmd,
};

REGISTER_APP_EX("super_spi", "0", "1\0gpio_port",
    &sspi_app_ops, "Unified SPI1+SPI2 bus master (gpio_port)");
