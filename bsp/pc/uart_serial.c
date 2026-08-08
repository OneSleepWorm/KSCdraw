/**
 * @file    uart_serial.c
 * @note    UART 串口应用 — PC 文件模拟 (stdin.txt / stdout.txt) (PC BSP)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  uart_serial
 * dep:     NULL
 * app_dep: "0"
 * 平台:    PC (__USE_PC__)
 *
 * ============================================================
 * appwrite — 发送 (mode 被忽略)
 * ============================================================
 *   向 stdout.txt 追加写 count 字节。
 *
 * ============================================================
 * appread — 接收 (mode 被忽略)
 * ============================================================
 *   从 stdin.txt 非阻塞读取，返回实际读到的字节数。
 *
 * ============================================================
 * appcmd — 控制命令
 * ============================================================
 *   open  -c <path> -o <path>   重定向 stdin/stdout 文件路径
 *   close                        关闭
 *   print -m <msg>               打印到控制台
 *   clear                         清屏
 *
 * ============================================================
 * 典型用法
 * ============================================================
 *   app_t* u = appget("uart_serial");
 *   appopen(u);
 *   appwrite(u, "Hello\r\n", 7, 0);   // 写 stdout.txt
 *   appread(u, buf, 64, 0);            // 读 stdin.txt
 *   appclose(u);
 *
 * ============================================================
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define PC_UART_RX_BUF_SIZE  256

typedef struct {
    char          stdin_path[64];
    char          stdout_path[64];
    FILE*         stdin_fp;
    FILE*         stdout_fp;
    long          stdin_offset;
    uint8_t       rx_buf[PC_UART_RX_BUF_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint32_t rx_overflow;
} pc_uart_ctx_t;

static void pc_uart_default_path(char* out, size_t sz, const char* filename)
{
    GetModuleFileNameA(NULL, out, (DWORD)sz);
    for (int i = 0; i < 2; i++) {
        char* sep = strrchr(out, '\\');
        if (sep) *sep = '\0';
    }
    strcat(out, "\\.data\\");
    strcat(out, filename);
}

/* drain stdin.txt into rx ring buffer (STM32 ISR semantics), then truncate */
static void pc_uart_drain_stdin(pc_uart_ctx_t* ctx)
{
    if (!ctx->stdin_fp) return;

    fseek(ctx->stdin_fp, 0, SEEK_END);
    long size = ftell(ctx->stdin_fp);
    if (size <= ctx->stdin_offset) return;

    fseek(ctx->stdin_fp, ctx->stdin_offset, SEEK_SET);

    long remaining = size - ctx->stdin_offset;
    if (remaining > (long)sizeof(ctx->rx_buf)) remaining = (long)sizeof(ctx->rx_buf);
    if (remaining > 0) {
        uint8_t tmp[256];
        if (remaining > (long)sizeof(tmp)) remaining = (long)sizeof(tmp);
        int n = (int)fread(tmp, 1, (size_t)remaining, ctx->stdin_fp);
        for (int i = 0; i < n; i++) {
            uint16_t next = (uint16_t)((ctx->rx_head + 1) & (PC_UART_RX_BUF_SIZE - 1));
            if (next != ctx->rx_tail) {
                ctx->rx_buf[ctx->rx_head] = tmp[i];
                ctx->rx_head = next;
            } else {
                ctx->rx_overflow++;
            }
        }
    }

    /* truncate stdin.txt in-place — simulate RXNE flag reset */
    ctx->stdin_fp = freopen(ctx->stdin_path, "w+b", ctx->stdin_fp);
    ctx->stdin_offset = 0;
}

static int pc_uart_open(app_t* app)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)osmalloc(sizeof(pc_uart_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(pc_uart_ctx_t));

    pc_uart_default_path(ctx->stdin_path, sizeof(ctx->stdin_path), "stdin.txt");
    pc_uart_default_path(ctx->stdout_path, sizeof(ctx->stdout_path), "stdout.txt");

    ctx->stdin_fp = fopen(ctx->stdin_path, "r+b");
    if (!ctx->stdin_fp) ctx->stdin_fp = fopen(ctx->stdin_path, "w+b");
    ctx->stdout_fp = fopen(ctx->stdout_path, "r+b");
    if (!ctx->stdout_fp) ctx->stdout_fp = fopen(ctx->stdout_path, "w+b");

    app->app_data = ctx;
    return 0;
}

static int pc_uart_close(app_t* app)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (ctx) {
        if (ctx->stdin_fp)  fclose(ctx->stdin_fp);
        if (ctx->stdout_fp) fclose(ctx->stdout_fp);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static int pc_uart_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !ctx->stdout_fp || !data || count == 0) return -1;

    fseek(ctx->stdout_fp, 0, SEEK_END);
    fwrite(data, 1, count, ctx->stdout_fp);
    fflush(ctx->stdout_fp);
    return (int)count;
}

static int pc_uart_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !data || !count) return 0;

    pc_uart_drain_stdin(ctx);

    uint32_t rd = 0;
    while (rd < count && ctx->rx_tail != ctx->rx_head) {
        ((uint8_t*)data)[rd] = ctx->rx_buf[ctx->rx_tail];
        ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) & (PC_UART_RX_BUF_SIZE - 1));
        rd++;
    }
    return (int)rd;
}

static int pc_uart_cmd(app_t* app, const char* cmdname, const char** argv)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (strcmp(cmdname, "print") == 0) {
        const char* msg = APPCMD_HAS(argv, 'm') ? argv[APPCMD_ARG('m')] : "";
        printf("%s\n", msg);
        return 0;
    }
    if (strcmp(cmdname, "clear") == 0) {
        printf("\033[2J\033[H");
        return 0;
    }
    if (strcmp(cmdname, "open") == 0) {
        if (!ctx) return -1;
        if (APPCMD_HAS(argv, 'c')) {
            if (ctx->stdin_fp) fclose(ctx->stdin_fp);
            strncpy(ctx->stdin_path, argv[APPCMD_ARG('c')], sizeof(ctx->stdin_path) - 1);
            ctx->stdin_fp = fopen(ctx->stdin_path, "w+b");
            ctx->stdin_offset = 0;
        }
        if (APPCMD_HAS(argv, 'o')) {
            if (ctx->stdout_fp) fclose(ctx->stdout_fp);
            strncpy(ctx->stdout_path, argv[APPCMD_ARG('o')], sizeof(ctx->stdout_path) - 1);
            ctx->stdout_fp = fopen(ctx->stdout_path, "w+b");
        }
        return 1;
    }
    if (strcmp(cmdname, "close") == 0) return 1;
    return -1;
}

static const papp_ops_t pc_uart_ops = {
    .open  = pc_uart_open,
    .close = pc_uart_close,
    .read  = pc_uart_read,
    .write = pc_uart_write,
    .cmd   = pc_uart_cmd,
};

REGISTER_APP_EX("uart_serial", "0", "0", &pc_uart_ops,
    "PC serial via stdin.txt / stdout.txt");
