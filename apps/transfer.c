#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "app_config.h"
#include "../third_party/async_xmodem/xmodem_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if __USE_STM32__ || __USE_PC__

static void transfer_tx_byte(struct xmodem_server *xdm, uint8_t byte, void *cb_data)
{
    (void)xdm;
    app_t* app = (app_t*)cb_data;
    transfer_ctx_t* ctx = (transfer_ctx_t*)app->app_data;
    if (ctx && ctx->uart)
        appwrite(ctx->uart, &byte, 1, 0);
}

static int transfer_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)cmdname;
    transfer_ctx_t* ctx = (transfer_ctx_t*)app->app_data;
    if (!ctx) return -1;

    const char* path = argv[APPCMD_ARG('p')];
    if (!path || !*path) return -1;

    uint32_t file_size = 0;
    const char* nstr = argv[APPCMD_ARG('n')];
    if (nstr) file_size = (uint32_t)strtoul(nstr, NULL, 0);

    app_t* uart = ctx->uart;
    app_t* lfs = ctx->lfs;
    if (!uart || !lfs) return -1;

    const char* oa[26] = {0};
    oa[APPCMD_ARG('p')] = path;
    oa[APPCMD_ARG('f')] = "0x502";
    int r = appcmd_argv(lfs, "open", oa);
    if (r < 0) return -1;

    xmodem_server_init(&ctx->xdm, transfer_tx_byte, app);
    ctx->active = 1;

    uint32_t written = 0;

    while (ctx->active) {
        uint8_t rxbuf[128];
        int n = appread(uart, rxbuf, sizeof(rxbuf), 1);
        for (int i = 0; i < n; i++)
            xmodem_server_rx_byte(&ctx->xdm, rxbuf[i]);

        uint8_t pkt[XMODEM_MAX_PACKET_SIZE];
        uint32_t blk;
        int rlen = xmodem_server_process(&ctx->xdm, pkt, &blk, sysgettime());
        if (rlen > 0) {
            uint32_t towrite = (uint32_t)rlen;
            if (file_size > 0 && written + towrite > file_size)
                towrite = file_size - written;
            lfs->input_data = pkt;
            char wn[12];
            snprintf(wn, sizeof(wn), "%lu", (unsigned long)towrite);
            const char* fa[26] = {0};
            fa[APPCMD_ARG('n')] = wn;
            int wr = appcmd_argv(lfs, "fwrite", fa);
            lfs->input_data = NULL;
            if (wr < 0) {
                ctx->active = 0;
                return -1;
            }
            written += towrite;
        }

        if (xmodem_server_is_done(&ctx->xdm)) {
            appcmd_argv(lfs, "close", NULL);
            ctx->active = 0;
            return 0;
        }

#if __USE_PC__
        Sleep(1);
#endif
    }

    return 0;
}

static int transfer_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static int transfer_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static int transfer_open(app_t* app)
{
    if (app->app_data) return 0;
    transfer_ctx_t* ctx = (transfer_ctx_t*)osmalloc(sizeof(transfer_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(transfer_ctx_t));
    ctx->uart = app->app0;
    ctx->lfs  = app->app1;
    app->app_data = ctx;
    return 0;
}

static int transfer_close(app_t* app)
{
    transfer_ctx_t* ctx = (transfer_ctx_t*)app->app_data;
    if (ctx) osfree(ctx);
    app->app_data = NULL;
    return 0;
}

static const papp_ops_t transfer_ops = {
    .open  = transfer_open,
    .close = transfer_close,
    .read  = transfer_read,
    .write = transfer_write,
    .cmd   = transfer_cmd,
};

REGISTER_APP_EX("transfer", "0", "2\0uart_serial\0littlefs", &transfer_ops,
    "File transfer via XMODEM to littlefs");

#endif
