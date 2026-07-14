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
 * appcmd 接口 (字符串命令):
 *   flash->user_data = buf;
 *   appcmd(flash, "id")                         → 返回 JEDEC ID
 *   appcmd(flash, "sr")                         → 返回 Status Reg 1
 *   appcmd(flash, "uid")                        → 8 B UID → buf
 *   appcmd(flash, "read  -a 0 -n 256")          → 读 n B → buf
 *   appcmd(flash, "fast  -a 0 -n 256")          → 快速读
 *   appcmd(flash, "write -a 1000 -n 16")        → 从 buf 写 n B
 *   appcmd(flash, "erase -a 0 -s 4096")         → 4K 扇区擦除
 *   appcmd(flash, "ce")                         → 全片擦除
 *
 * 传统 appwrite/appread 接口不变:
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
#include <string.h>
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

    ctx->dev_id = appcmd(ctx->sspi, "reg -i 2");
    if (ctx->dev_id < 0) { appclose(ctx->sspi); osfree(ctx); return -1; }
    sspi_setpin(ctx->sspi, 2, ctx->dev_id, SSPI_CS, CS_PIN);
    sspi_setpin(ctx->sspi, 2, ctx->dev_id, SSPI_R1, RST_PIN);

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

/* ================================================================
 * appcmd 接口
 * ================================================================
 *
 *   数据通过 app->user_data 传递（读: 调用方设缓冲 → handler 写入;
 *   写: 调用方设数据指针 → handler 读出）。
 *
 *   id                               返回 JEDEC ID (如 0xEF4017)
 *   sr                               返回 Status Register 1
 *   uid                              读 8 字节 UID → user_data
 *   read -a <addr> -n <len>          标准读 (0x03), 结果 → user_data
 *   fast -a <addr> -n <len>          快速读 (0x0B + 1 dummy)
 *   write -a <addr> -n <len>         页写 (0x02, ≤256 B), 数据从 user_data 取
 *   erase -a <addr> -s <size>        擦除: -s 4096/32768/65536
 *   ce                               全片擦除
 */

typedef struct { const char* name; int (*handler)(app_t*, const char**); } w25_cmd_t;

static int cmd_id(app_t* app, const char** argv)
{
    (void)argv;
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx) return -1;
    uint8_t id[3], c = 0x9F;
    w25_cs_low(ctx);
    w25_xfer(ctx, &c, 1, id, 3);
    w25_cs_high(ctx);
    return (id[0] << 16) | (id[1] << 8) | id[2];
}

static int cmd_sr(app_t* app, const char** argv)
{
    (void)argv;
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx) return -1;
    uint8_t sr, c = 0x05;
    w25_cs_low(ctx);
    w25_xfer(ctx, &c, 1, &sr, 1);
    w25_cs_high(ctx);
    return sr;
}

static int cmd_uid(app_t* app, const char** argv)
{
    (void)argv;
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx || !app->user_data) return -1;
    uint8_t hdr[5] = {0x4B, 0xFF, 0xFF, 0xFF, 0xFF};
    w25_cs_low(ctx);
    w25_xfer(ctx, hdr, 5, (uint8_t*)app->user_data, 8);
    w25_cs_high(ctx);
    return 8;
}

static int cmd_read(app_t* app, const char** argv)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 'n')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint16_t n = (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0 || !app->user_data) return -1;
    uint8_t cmd[4] = {0x03, (addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF};
    w25_cs_low(ctx);
    w25_xfer(ctx, cmd, 4, (uint8_t*)app->user_data, n);
    w25_cs_high(ctx);
    return (int)n;
}

static int cmd_fast(app_t* app, const char** argv)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 'n')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint16_t n = (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0 || !app->user_data) return -1;
    uint8_t cmd[5] = {0x0B, (addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF, 0xFF};
    w25_cs_low(ctx);
    w25_xfer(ctx, cmd, 5, (uint8_t*)app->user_data, n);
    w25_cs_high(ctx);
    return (int)n;
}

static int cmd_write(app_t* app, const char** argv)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 'n')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint16_t n = (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0 || n > 256 || !app->user_data) return -1;
    uint8_t* src = (uint8_t*)app->user_data;
    w25_wait_ready(ctx);
    w25_we(ctx);
    uint8_t hdr[4] = {0x02, (addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF};
    w25_cs_low(ctx);
    w25_xfer(ctx, hdr, 4, NULL, 0);
    w25_xfer(ctx, src, n, NULL, 0);
    w25_cs_high(ctx);
    w25_wait_ready(ctx);
    return (int)n;
}

static int cmd_erase(app_t* app, const char** argv)
{
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 's')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint32_t size = strtoul(argv[APPCMD_ARG('s')], NULL, 0);
    uint8_t op;
    if (size == 4096) op = 0x20;
    else if (size == 32768) op = 0x52;
    else if (size == 65536) op = 0xD8;
    else return -1;
    w25_wait_ready(ctx);
    w25_we(ctx);
    uint8_t c[4] = {op, (addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF};
    w25_cs_low(ctx);
    w25_xfer(ctx, c, 4, NULL, 0);
    w25_cs_high(ctx);
    w25_wait_ready(ctx);
    return 0;
}

static int cmd_ce(app_t* app, const char** argv)
{
    (void)argv;
    w25_ctx_t* ctx = (w25_ctx_t*)app->app_data;
    if (!ctx) return -1;
    w25_wait_ready(ctx);
    w25_we(ctx);
    uint8_t c = 0xC7;
    w25_cs_low(ctx);
    w25_xfer(ctx, &c, 1, NULL, 0);
    w25_cs_high(ctx);
    w25_wait_ready(ctx);
    return 0;
}

static int w25_cmd(app_t* app, const char* cmdname, const char** argv)
{
    static const w25_cmd_t table[] = {
        {"id",    cmd_id},
        {"sr",    cmd_sr},
        {"uid",   cmd_uid},
        {"read",  cmd_read},
        {"fast",  cmd_fast},
        {"write", cmd_write},
        {"erase", cmd_erase},
        {"ce",    cmd_ce},
        {NULL, NULL}
    };
    for (const w25_cmd_t* e = table; e->name; e++) {
        if (strcmp(cmdname, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t w25_app_ops = {
    .open  = w25_app_open,
    .close = w25_app_close,
    .write = w25_app_write,
    .read  = w25_app_read,
    .cmd   = w25_cmd,
};

REGISTER_APP_EX("w25qxx_base", "0", "1\0super_spi", &w25_app_ops, "W25Q64 SPI NOR Flash");

#endif
