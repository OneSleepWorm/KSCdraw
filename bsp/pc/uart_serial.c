/**
 * @file    uart_serial.c
 * @note    UART 串口应用 — PC 文件模拟, 多通道 (stdinN.txt / stdoutN.txt) (PC BSP)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  uart_serial
 * dep:     NULL
 * app_dep: "0"
 * 平台:    PC (__USE_PC__)
 * 通道数:  __UART_CHANNELS__ (KSCconfig.h), 默认通道 __UART_DEFAULT_CHANNEL__
 *
 * ============================================================
 * 通道模型
 * ============================================================
 *   每个通道 N (1..__UART_CHANNELS__) 对应一对文件:
 *     KSCOS/.data/stdinN.txt   (daemon → KSCOS 输入)
 *     KSCOS/.data/stdoutN.txt  (KSCOS → daemon 输出)
 *   通道 1 默认与 monitor daemon 对接 (见 tools/monitor/cli.py 默认路径)。
 *
 * ============================================================
 * appwrite — 发送 (mode 被忽略)
 * ============================================================
 *   总是向默认通道 (dflt_inst) 追加写 count 字节到 stdoutN.txt。
 *   首次调用时自动懒初始化默认通道。
 *
 * ============================================================
 * appread — 接收 (mode 被忽略)
 * ============================================================
 *   总是从默认通道 (dflt_inst) 非阻塞读取，返回实际读到的字节数。
 *
 * ============================================================
 * appcmd — 控制命令 (标准动作, 与 STM32 同规范)
 * ============================================================
 *   open           打开默认通道 (config 默认)
 *   open -i <N>    打开通道 N
 *   close -i <N>   关闭通道 N
 *   dflt -i <N>    设默认通道 (ksc_console 跟随)
 *   rd    -i <N>   读通道状态 → output_data
 *   baud  -i <N> -b <baud>  不支持 (平台可选, 返回 -1)
 *   rxirq -i <N>   不支持 (平台可选, 返回 -1)
 *
 * ============================================================
 * 典型用法
 * ============================================================
 *   app_t* u = appget("uart_serial");
 *   appopen(u);
 *   appcmd(u, "open");              // 打开默认通道 (config 决定)
 *   appwrite(u, "Hello\r\n", 7, 0); // 写默认通道 stdoutN.txt
 *   appread(u, buf, 64, 0);          // 读默认通道 stdinN.txt
 *   appclose(u);
 *
 * ============================================================
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/KSCconfig.h"
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
} pc_uart_port_t;

typedef struct {
    pc_uart_port_t port[__UART_CHANNELS__];
    uint8_t        enabled;       /* bit(N-1)=通道 N 已打开 */
    uint8_t        dflt_inst;     /* 默认通道 (1..channels) */
    uint32_t       rd_val;
} pc_uart_ctx_t;

static void pc_uart_default_path(char* out, size_t sz, int inst, const char* basename)
{
    GetModuleFileNameA(NULL, out, (DWORD)sz);
    for (int i = 0; i < 2; i++) {
        char* sep = strrchr(out, '\\');
        if (sep) *sep = '\0';
    }
    strcat(out, "\\.data\\");
    strcat(out, basename);
    char num[4];
    snprintf(num, sizeof(num), "%d", inst);
    strcat(out, num);
    strcat(out, ".txt");
}

/* 确保 .data 目录存在 (与 bsp/pc/w25qxx_base.c 的 pc_flash_mkdir 保持一致)。
 * fopen/freopen 不会自动创建不存在的父目录; 干净环境(新克隆/CI)下 .data 不存在,
 * 若不先建目录, stdinN.txt/stdoutN.txt 打开会静默失败, 通信链断裂。 */
static void pc_uart_mkdir(const char* path)
{
    char dir[MAX_PATH];
    strcpy(dir, path);
    char* sep = strrchr(dir, '\\');
    if (sep) { *sep = '\0'; CreateDirectoryA(dir, NULL); }
}

/* drain stdinN.txt into rx ring buffer (STM32 ISR semantics), then truncate */
static void pc_uart_drain_stdin(pc_uart_port_t* p)
{
    if (!p->stdin_fp) return;

    fseek(p->stdin_fp, 0, SEEK_END);
    long size = ftell(p->stdin_fp);
    if (size <= p->stdin_offset) return;

    fseek(p->stdin_fp, p->stdin_offset, SEEK_SET);

    long remaining = size - p->stdin_offset;
    if (remaining > (long)sizeof(p->rx_buf)) remaining = (long)sizeof(p->rx_buf);
    if (remaining > 0) {
        uint8_t tmp[256];
        if (remaining > (long)sizeof(tmp)) remaining = (long)sizeof(tmp);
        int n = (int)fread(tmp, 1, (size_t)remaining, p->stdin_fp);
        for (int i = 0; i < n; i++) {
            uint16_t next = (uint16_t)((p->rx_head + 1) & (PC_UART_RX_BUF_SIZE - 1));
            if (next != p->rx_tail) {
                p->rx_buf[p->rx_head] = tmp[i];
                p->rx_head = next;
            } else {
                p->rx_overflow++;
            }
        }
    }

    /* truncate stdinN.txt in-place — simulate RXNE flag reset */
    p->stdin_fp = freopen(p->stdin_path, "w+b", p->stdin_fp);
    p->stdin_offset = 0;
}

static int port_open(pc_uart_ctx_t* ctx, int inst)
{
    if (inst < 1 || inst > __UART_CHANNELS__) return -1;
    pc_uart_port_t* p = &ctx->port[inst - 1];
    uint8_t bit = (uint8_t)(1 << (inst - 1));
    if (ctx->enabled & bit) return 1;

    pc_uart_default_path(p->stdin_path, sizeof(p->stdin_path), inst, "stdin");
    pc_uart_default_path(p->stdout_path, sizeof(p->stdout_path), inst, "stdout");

    pc_uart_mkdir(p->stdin_path);   /* 确保 .data 目录存在 */

    p->stdin_fp = fopen(p->stdin_path, "r+b");
    if (!p->stdin_fp) p->stdin_fp = fopen(p->stdin_path, "w+b");
    p->stdout_fp = fopen(p->stdout_path, "r+b");
    if (!p->stdout_fp) p->stdout_fp = fopen(p->stdout_path, "w+b");

    ctx->enabled |= bit;
    return 1;
}

static void port_close(pc_uart_ctx_t* ctx, int inst)
{
    pc_uart_port_t* p = &ctx->port[inst - 1];
    if (p->stdin_fp)  { fclose(p->stdin_fp);  p->stdin_fp  = NULL; }
    if (p->stdout_fp) { fclose(p->stdout_fp); p->stdout_fp = NULL; }
    ctx->enabled &= (uint8_t)~(1 << (inst - 1));
}

static int pc_uart_open(app_t* app)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)osmalloc(sizeof(pc_uart_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(pc_uart_ctx_t));
    ctx->dflt_inst = (uint8_t)__UART_DEFAULT_CHANNEL__;
    app->app_data = ctx;
    app->output_data = &ctx->rd_val;
    return 0;
}

static int pc_uart_close(app_t* app)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (ctx) {
        for (int i = 1; i <= __UART_CHANNELS__; i++)
            if (ctx->enabled & (1 << (i - 1)))
                port_close(ctx, i);
        osfree(ctx);
        app->app_data = NULL;
        app->output_data = NULL;
    }
    return 0;
}

static int pc_uart_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !data || count == 0) return -1;

    int inst = ctx->dflt_inst;
    port_open(ctx, inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    pc_uart_port_t* p = &ctx->port[inst - 1];
    if (!p->stdout_fp) return -1;

    fseek(p->stdout_fp, 0, SEEK_END);
    fwrite(data, 1, count, p->stdout_fp);
    fflush(p->stdout_fp);
    return (int)count;
}

static int pc_uart_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !data || !count) return 0;

    int inst = ctx->dflt_inst;
    if (!(ctx->enabled & (1 << (inst - 1)))) return 0;
    pc_uart_port_t* p = &ctx->port[inst - 1];

    pc_uart_drain_stdin(p);

    uint32_t rd = 0;
    while (rd < count && p->rx_tail != p->rx_head) {
        ((uint8_t*)data)[rd] = p->rx_buf[p->rx_tail];
        p->rx_tail = (uint16_t)((p->rx_tail + 1) & (PC_UART_RX_BUF_SIZE - 1));
        rd++;
    }
    return (int)rd;
}

static int cmd_open(app_t* app, const char** argv)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx) return -1;
    int inst = APPCMD_HAS(argv, 'i') ? (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0)
                                     : (int)ctx->dflt_inst;
    return port_open(ctx, inst);
}

static int cmd_close(app_t* app, const char** argv)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    int inst = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > __UART_CHANNELS__) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    port_close(ctx, inst);
    return 1;
}

static int cmd_baud(app_t* app, const char** argv)
{
    (void)app; (void)argv;
    return -1;   /* 平台可选: PC 无波特率概念 */
}

static int cmd_rxirq(app_t* app, const char** argv)
{
    (void)app; (void)argv;
    return -1;   /* 平台可选: PC 无硬件中断 */
}

static int cmd_dflt(app_t* app, const char** argv)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    int inst = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > __UART_CHANNELS__) return -1;
    ctx->dflt_inst = (uint8_t)inst;
    return 1;
}

static int cmd_rd(app_t* app, const char** argv)
{
    pc_uart_ctx_t* ctx = (pc_uart_ctx_t*)app->app_data;
    if (!ctx || !APPCMD_HAS(argv, 'i')) return -1;
    int inst = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (inst < 1 || inst > __UART_CHANNELS__) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;
    if (app->output_data) *(uint32_t*)app->output_data = (uint32_t)inst;
    return 1;
}

typedef int (*uart_cmd_h)(app_t*, const char**);
typedef struct { const char* name; uart_cmd_h handler; } uart_cmd_t;

static const uart_cmd_t uart_cmds[] = {
    {"open",  cmd_open},
    {"close", cmd_close},
    {"baud",  cmd_baud},
    {"rxirq", cmd_rxirq},
    {"dflt",  cmd_dflt},
    {"rd",    cmd_rd},
    {NULL, NULL}
};

static int uart_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const uart_cmd_t* e = uart_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t pc_uart_ops = {
    .open  = pc_uart_open,
    .close = pc_uart_close,
    .read  = pc_uart_read,
    .write = pc_uart_write,
    .cmd   = uart_app_cmd,
};

REGISTER_APP_EX("uart_serial", "0", "0", &pc_uart_ops,
    "PC serial via stdinN.txt / stdoutN.txt (multi-channel)");
