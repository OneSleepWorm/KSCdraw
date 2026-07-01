/**
 * @file    w25qxx_base.c
 * @note    W25Q64 SPI NOR Flash 基础驱动 (app 层, 依赖 super_spi2)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  w25qxx_base
 * dep:     NULL
 * app_dep: "1\0super_spi"
 * 平台:    STM32 (__USE_STM32__)
 * CS 引脚: PB11 (通过 super_spi2 mode=5 运行时重映射)
 *
 * ============================================================
 * 资源占用 (LTO差分法)
 * ============================================================
 *   ROM(Debug -O0):   1,576 B
 *   ROM(Release -Os):  1,008 B
 *   RAM(静态):   0 B
 *   RAM(堆):     8 B (w25_ctx_t, osmalloc 于 appopen)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * appget("w25qxx_base") → app_t*
 *   打开前无前置条件.
 *
 * appopen(flash) → int
 *   返回 0=在线, 非0=检测失败.
 *   内部打开 super_spi2, 设 CS=PB11, 读 JEDEC ID 验证.
 *   必须先设地址 (mode=1) 再读写.
 *
 * appwrite(flash, data, count, mode)
 *   1  — data=uint32_t addr. 设地址.
 *   2  — Write Enable.
 *   3  — Page Program. data=纯数据. 自动 WE + 等待完成.
 *   4  — Write SR1. data[0]=新值.
 *   5  — Sector Erase 4KB. 地址来自 ctx->addr.
 *   6  — Chip Erase.
 *   0  — no-op.
 *
 * appread(flash, data, count, mode)
 *   1  — Read Data. data=纯输出缓冲.
 *   2  — Fast Read. 同上 (8 dummy 时钟内部处理).
 *   3  — JEDEC ID. data[0..2]=3 字节 ID.
 *   4  — Read SR1. data[0]=SR 值.
 *   5  — Unique ID. data[0..7]=8 字节 UID.
 *   0  — no-op.
 *
 * 典型用法:
 *
 *   app_t* flash = appget("w25qxx_base");
 *   appopen(flash);
 *
 *   uint8_t id[3];
 *   appread(flash, id, 3, 3);
 *
 *   uint32_t addr = 0x000000;
 *   appwrite(flash, &addr, 4, 1);
 *   appwrite(flash, NULL, 0, 5);
 *
 *   uint8_t pg[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
 *   appwrite(flash, pg, 16, 3);
 *
 *   uint8_t rd[16];
 *   appread(flash, rd, 16, 1);
 *
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 所有写入/擦除类操作 (mode 3~6) 内部自动 WE + 等待完成.
 * 2. mode=0 为 no-op.
 * 3. 纯 super_spi (SPI2) 依赖, 通过 appwrite 完成 SPI 通信.
 * 4. appopen 时通过 super_spi2 mode=5 重设 CS=PB11/DC=PB0/RST=PB1.
 * 5. Wait busy 为内置自动行为, 无需显式调用.
 * 6. CS 由本层通过 mode 22(↓) / 23(↑) 控制, 零拷贝原始数据传输.
 */

#include "../inc/app.h"
#include "../inc/super_spi.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__

#define CS_PIN  11
#define DC_PIN  0
#define RST_PIN 1

typedef struct {
    app_t* sspi;
    int    dev_id;
    uint8_t spi_inst;
    uint32_t addr;
} w25_ctx_t;

static void w25_cs_low(w25_ctx_t* ctx)
{
    appwrite(ctx->sspi, NULL, 0, SSPI_MODE(ctx->spi_inst, ctx->dev_id, SSPI_CS_LOW));
}

static void w25_cs_high(w25_ctx_t* ctx)
{
    appwrite(ctx->sspi, NULL, 0, SSPI_MODE(ctx->spi_inst, ctx->dev_id, SSPI_CS_HIGH));
}

static void w25_xfer(w25_ctx_t* ctx, const void* tx, uint16_t txlen,
                     void* rx, uint16_t rxlen)
{
    spi_xfer_t x;
    x.tx_buf = (void*)tx;
    x.tx_len = txlen;
    x.rx_buf = rx;
    x.rx_len = rxlen;
    appwrite(ctx->sspi, &x, 1, SSPI_XFER_INST(ctx->spi_inst));
}

static void w25_we(w25_ctx_t* ctx)
{
    uint8_t cmd = 0x06;
    w25_cs_low(ctx);
    w25_xfer(ctx, &cmd, 1, NULL, 0);
    w25_cs_high(ctx);
}

static void w25_wait_ready(w25_ctx_t* ctx)
{
    uint8_t sr;
    do {
        uint8_t cmd = 0x05;
        w25_cs_low(ctx);
        w25_xfer(ctx, &cmd, 1, &sr, 1);
        w25_cs_high(ctx);
    } while (sr & 0x01);
}

static int w25_app_open(app_t* app)
{
    w25_ctx_t* ctx = osmalloc(sizeof(w25_ctx_t));
    if (!ctx) return -1;

    ctx->sspi = app->app0;
    ctx->spi_inst = 2;
    app->app_data = ctx;

    appopen(ctx->sspi);

    ctx->dev_id = appioctl(ctx->sspi, "reg", 2);
    if (ctx->dev_id < 0) { appclose(ctx->sspi); osfree(ctx); return -1; }
    appioctl(ctx->sspi, "setpin", 2, ctx->dev_id, SSPI_CS, CS_PIN);
    appioctl(ctx->sspi, "setpin", 2, ctx->dev_id, SSPI_R1, RST_PIN);

    uint8_t cmd = 0x9F, id[3];
    w25_cs_low(ctx);
    w25_xfer(ctx, &cmd, 1, id, 3);
    w25_cs_high(ctx);
    if (id[0] == 0xFF && id[1] == 0xFF) return -1;
    if (id[0] == 0x00 && id[1] == 0x00) return -1;

    return 0;
}

static int w25_app_close(app_t* app)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (ctx) {
        appclose(ctx->sspi);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static int w25_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx) return -1;
    uint8_t* d = (uint8_t*)data;

    switch (mode) {

    case 0:
        return 0;

    case 1:
        if (count < 4) return -1;
        ctx->addr = *(uint32_t*)data;
        return 4;

    case 2:
        w25_we(ctx);
        return 0;

    case 3:
        if (!count) return -1;
        w25_wait_ready(ctx);
        w25_we(ctx);
        {
            uint32_t a = ctx->addr;
            uint8_t hdr[4] = {0x02, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF};
            w25_cs_low(ctx);
            w25_xfer(ctx, hdr, 4, NULL, 0);
            w25_xfer(ctx, d, count, NULL, 0);
            w25_cs_high(ctx);
        }
        w25_wait_ready(ctx);
        return (int)count;

    case 4:
        if (!count) return -1;
        w25_wait_ready(ctx);
        w25_we(ctx);
        {
            uint8_t c[2] = {0x01, d[0]};
            w25_cs_low(ctx);
            w25_xfer(ctx, c, 2, NULL, 0);
            w25_cs_high(ctx);
        }
        w25_wait_ready(ctx);
        return 1;

    case 5:
        w25_wait_ready(ctx);
        w25_we(ctx);
        {
            uint32_t a = ctx->addr;
            uint8_t c[4] = {0x20, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF};
            w25_cs_low(ctx);
            w25_xfer(ctx, c, 4, NULL, 0);
            w25_cs_high(ctx);
        }
        w25_wait_ready(ctx);
        return 0;

    case 6:
        w25_wait_ready(ctx);
        w25_we(ctx);
        {
            uint8_t c = 0xC7;
            w25_cs_low(ctx);
            w25_xfer(ctx, &c, 1, NULL, 0);
            w25_cs_high(ctx);
        }
        w25_wait_ready(ctx);
        return 0;

    default:
        return -1;
    }
}

static int w25_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx) return -1;
    uint8_t* d = (uint8_t*)data;

    switch (mode) {

    case 0:
        return 0;

    case 1:
        if (!count) return -1;
        {
            uint32_t a = ctx->addr;
            uint8_t cmd[4] = {0x03, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF};
            w25_cs_low(ctx);
            w25_xfer(ctx, cmd, 4, d, count);
            w25_cs_high(ctx);
        }
        return (int)count;

    case 2:
        if (!count) return -1;
        {
            uint32_t a = ctx->addr;
            uint8_t cmd[5] = {0x0B, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF, 0xFF};
            w25_cs_low(ctx);
            w25_xfer(ctx, cmd, 5, d, count);
            w25_cs_high(ctx);
        }
        return (int)count;

    case 3:
    {
        uint8_t cmd = 0x9F;
        w25_cs_low(ctx);
        w25_xfer(ctx, &cmd, 1, d, 3);
        w25_cs_high(ctx);
        return 3;
    }

    case 4:
    {
        uint8_t cmd = 0x05;
        w25_cs_low(ctx);
        w25_xfer(ctx, &cmd, 1, d, 1);
        w25_cs_high(ctx);
        return 1;
    }

    case 5:
    {
        uint8_t cmd[5] = {0x4B, 0xFF, 0xFF, 0xFF, 0xFF};
        w25_cs_low(ctx);
        w25_xfer(ctx, cmd, 5, d, 8);
        w25_cs_high(ctx);
        return 8;
    }

    default:
        return -1;
    }
}

static const papp_ops_t w25_app_ops = {
    .open  = w25_app_open,
    .close = w25_app_close,
    .write = w25_app_write,
    .read  = w25_app_read,
};

REGISTER_APP_EX("w25qxx_base", "0", "1\0super_spi", &w25_app_ops, "W25Q64 SPI NOR Flash");

#endif
