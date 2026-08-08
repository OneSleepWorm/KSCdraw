/**
 * @file    w25qxx_base.c
 * @note    W25Q64 SPI NOR Flash 基础驱动 (PC BSP, 文件模拟)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  w25qxx_base
 * dep:     NULL
 * app_dep: "0"
 * 平台:    PC (__USE_PC__)
 * 后端:    .data/flash.bin 文件模拟 (2048×4096 = 8MB, 0xFF 初始化)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * appget("w25qxx_base") → app_t*
 * appopen(flash) → 打开/创建 flash.bin, 返回 0=在线.
 * appwrite(flash, data, count, mode)
 *   1  — data=uint32_t addr. 设地址.
 *   3  — Page Program. data=纯数据. 直接文件写.
 *   5  — Sector Erase 4KB. 写 0xFF.
 *   6  — Chip Erase. 全片 0xFF.
 *   0  — no-op.
 * appread(flash, data, count, mode)
 *   1  — Read Data. 从文件读.
 *   3  — JEDEC ID. 返回 0xEF4017.
 *   0  — no-op.
 *
 * appcmd 接口 (字符串命令, 与 STM32 命令集对齐):
 *   id / sr / uid / read / fast / write / erase / ce
 *   ⚠️ write/erase/ce 为破坏性命令 (见下方警告)
 *
 * ⚠️ 破坏性命令警告: write/erase/ce 会改写/擦除 flash 原始数据, 直接摧毁
 * littlefs 文件系统 (相当于删掉 .data/flash.bin)。littlefs 通过二进制接口
 * (appwrite mode 1/3/5) 使用 flash, 正常流程与测试均不需要这些命令。
 * 仅开发者底层调试时使用; 执行 ce 前务必确认。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#define PC_FLASH_SIZE     (2048 * 4096)
#define PC_FLASH_SECTOR   4096

typedef struct {
    FILE*  fp;
    uint32_t addr;
    char   path[MAX_PATH];
} pc_flash_ctx_t;

static void pc_flash_path(char* path, size_t sz)
{
    GetModuleFileNameA(NULL, path, (DWORD)sz);
    for (int i = 0; i < 2; i++) {
        char* sep = strrchr(path, '\\');
        if (sep) *sep = '\0';
    }
    strcat(path, "\\.data\\flash.bin");
}

static void pc_flash_mkdir(const char* path)
{
    char dir[MAX_PATH];
    strcpy(dir, path);
    char* sep = strrchr(dir, '\\');
    if (sep) { *sep = '\0'; CreateDirectoryA(dir, NULL); }
}

static int pc_flash_open(app_t* app)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)osmalloc(sizeof(pc_flash_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));

    pc_flash_path(ctx->path, sizeof(ctx->path));
    pc_flash_mkdir(ctx->path);

    ctx->fp = fopen(ctx->path, "r+b");
    if (!ctx->fp) {
        ctx->fp = fopen(ctx->path, "wb");
        if (!ctx->fp) { osfree(ctx); return -1; }
        uint8_t buf[PC_FLASH_SECTOR];
        memset(buf, 0xFF, PC_FLASH_SECTOR);
        for (uint32_t i = 0; i < PC_FLASH_SIZE / PC_FLASH_SECTOR; i++)
            fwrite(buf, 1, PC_FLASH_SECTOR, ctx->fp);
        fclose(ctx->fp);
        ctx->fp = fopen(ctx->path, "r+b");
        if (!ctx->fp) { osfree(ctx); return -1; }
    }
    app->app_data = ctx;
    return 0;
}

static int pc_flash_close(app_t* app)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (ctx) {
        if (ctx->fp) fclose(ctx->fp);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static int pc_flash_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !ctx->fp) return -1;

    switch (mode) {
    case 0: return 0;
    case 1:
        if (count < 4) return -1;
        ctx->addr = *(uint32_t*)data;
        return 4;
    case 3:
        if (!data || !count) return -1;
        fseek(ctx->fp, ctx->addr, SEEK_SET);
        fwrite(data, 1, count, ctx->fp);
        return (int)count;
    case 5:
    {
        uint8_t buf[PC_FLASH_SECTOR];
        memset(buf, 0xFF, PC_FLASH_SECTOR);
        fseek(ctx->fp, ctx->addr, SEEK_SET);
        fwrite(buf, 1, PC_FLASH_SECTOR, ctx->fp);
        return 0;
    }
    case 6:
    {
        uint8_t buf[PC_FLASH_SECTOR];
        memset(buf, 0xFF, PC_FLASH_SECTOR);
        fseek(ctx->fp, 0, SEEK_SET);
        for (uint32_t i = 0; i < PC_FLASH_SIZE / PC_FLASH_SECTOR; i++)
            fwrite(buf, 1, PC_FLASH_SECTOR, ctx->fp);
        return 0;
    }
    default: return -1;
    }
}

static int pc_flash_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !ctx->fp) return -1;

    switch (mode) {
    case 0: return 0;
    case 1:
        if (!data || !count) return -1;
        fseek(ctx->fp, ctx->addr, SEEK_SET);
        return (int)fread(data, 1, count, ctx->fp);
    case 3:
        ((uint8_t*)data)[0] = 0xEF;
        ((uint8_t*)data)[1] = 0x40;
        ((uint8_t*)data)[2] = 0x17;
        return 3;
    default: return -1;
    }
}

/* PC appcmd — 与 STM32 w25_cmd 命令集对齐。
 * 底层复用 pc_flash_read/write 的二进制 mode (1=读, 3=写, 5=扇区擦除, 6=全片)。 */
static int pc_cmd_id(app_t* app, const char** argv)
{
    (void)argv;
    (void)app;
    return 0xEF4017;   /* W25Q64 JEDEC ID, 与 STM32 一致 */
}

static int pc_cmd_sr(app_t* app, const char** argv)
{
    (void)argv;
    (void)app;
    return 0;          /* PC 无真实状态寄存器 */
}

static int pc_cmd_uid(app_t* app, const char** argv)
{
    (void)argv;
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !app->input_data) return -1;
    uint8_t* dst = (uint8_t*)app->input_data;
    for (int i = 0; i < 8; i++) dst[i] = (uint8_t)(0x10 + i);
    return 8;
}

static int pc_cmd_read(app_t* app, const char** argv, int fast)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 'n')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint16_t n = (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0 || !app->input_data) return -1;
    if (addr + n > PC_FLASH_SIZE) return -1;
    (void)fast;   /* PC 上 fast 读与标准读等价 */
    fseek(ctx->fp, (long)addr, SEEK_SET);
    return (int)fread(app->input_data, 1, n, ctx->fp) == (int)n ? (int)n : -1;
}

static int pc_cmd_write(app_t* app, const char** argv)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 'n')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint16_t n = (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0 || n > 256 || !app->input_data) return -1;
    if (addr + n > PC_FLASH_SIZE) return -1;
    fseek(ctx->fp, (long)addr, SEEK_SET);
    fwrite(app->input_data, 1, n, ctx->fp);
    return (int)n;
}

static int pc_cmd_erase(app_t* app, const char** argv)
{
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'a') || !APPCMD_HAS(argv, 's')) return -1;
    uint32_t addr = strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    uint32_t size = strtoul(argv[APPCMD_ARG('s')], NULL, 0);
    if (size == 0 || size % PC_FLASH_SECTOR != 0) return -1;
    if (addr + size > PC_FLASH_SIZE) return -1;
    uint8_t* buf = (uint8_t*)osmalloc(size);
    if (!buf) return -1;
    memset(buf, 0xFF, size);
    fseek(ctx->fp, (long)addr, SEEK_SET);
    int w = (int)fwrite(buf, 1, size, ctx->fp);
    osfree(buf);
    return w == (int)size ? 0 : -1;
}

static int pc_cmd_ce(app_t* app, const char** argv)
{
    (void)argv;
    pc_flash_ctx_t* ctx = (pc_flash_ctx_t*)app->app_data;
    if (!ctx) return -1;
    uint8_t* buf = (uint8_t*)osmalloc(PC_FLASH_SECTOR);
    if (!buf) return -1;
    memset(buf, 0xFF, PC_FLASH_SECTOR);
    fseek(ctx->fp, 0, SEEK_SET);
    for (uint32_t i = 0; i < PC_FLASH_SIZE / PC_FLASH_SECTOR; i++)
        fwrite(buf, 1, PC_FLASH_SECTOR, ctx->fp);
    osfree(buf);
    return 0;
}

static int pc_flash_cmd(app_t* app, const char* cmdname, const char** argv)
{
    if (!app) return -1;
    if (strcmp(cmdname, "id") == 0)    return pc_cmd_id(app, argv);
    if (strcmp(cmdname, "sr") == 0)    return pc_cmd_sr(app, argv);
    if (strcmp(cmdname, "uid") == 0)   return pc_cmd_uid(app, argv);
    if (strcmp(cmdname, "read") == 0)  return pc_cmd_read(app, argv, 0);
    if (strcmp(cmdname, "fast") == 0)  return pc_cmd_read(app, argv, 1);
    if (strcmp(cmdname, "write") == 0) return pc_cmd_write(app, argv);
    if (strcmp(cmdname, "erase") == 0) return pc_cmd_erase(app, argv);
    if (strcmp(cmdname, "ce") == 0)    return pc_cmd_ce(app, argv);
    return -1;
}

static const papp_ops_t pc_flash_ops = {
    .open  = pc_flash_open,
    .close = pc_flash_close,
    .read  = pc_flash_read,
    .write = pc_flash_write,
    .cmd   = pc_flash_cmd,
};

REGISTER_APP_EX("w25qxx_base", "0", "0", &pc_flash_ops,
    "PC file-backed flash emulation");
