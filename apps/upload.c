#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if __USE_STM32__

static int upload_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)cmdname;
    upload_ctx_t* ctx = (upload_ctx_t*)app->app_data;
    if (!ctx) return -1;

    const char* path = argv[APPCMD_ARG('p')];
    const char* nstr = argv[APPCMD_ARG('n')];
    if (!path || !nstr) return -1;

    uint32_t size = (uint32_t)strtoul(nstr, NULL, 0);
    if (size == 0) return -1;

    app_t* lfs = ctx->lfs ? ctx->lfs : app->app0;
    if (!lfs) return -1;
    appopen(lfs);

    const char* oa[26] = {0};
    oa[APPCMD_ARG('p')] = path;
    oa[APPCMD_ARG('f')] = "0x502";
    int r = appcmd_argv(lfs, "open", oa);
    if (r < 0) return -1;

    ctx->lfs = lfs;
    ctx->remaining = size;
    ctx->chunk_len = 0;
    ctx->active = 1;

    uint8_t ack = '!';
    appwrite(ksc_console, &ack, 1, 0x11);

    return 0;
}

static int upload_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)mode;
    upload_ctx_t* ctx = (upload_ctx_t*)app->app_data;
    if (!ctx || !ctx->active) return -1;

    if (count == 0 || ctx->remaining == 0) return 0;

    if (ctx->chunk_len == 0 && count == 1 && ((const uint8_t*)data)[0] == '\n') {
        return 1;
    }

    const uint8_t* src = (const uint8_t*)data;
    uint32_t consumed = 0;

    while (consumed < count && ctx->remaining > 0) {
        uint32_t avail = count - consumed;
        uint32_t space = 256 - ctx->chunk_len;
        uint32_t copy = (avail < space) ? avail : space;
        if (copy > ctx->remaining) copy = ctx->remaining;

        memcpy(ctx->chunk + ctx->chunk_len, src + consumed, copy);
        ctx->chunk_len += copy;
        consumed += copy;
        ctx->remaining -= copy;

        if (ctx->chunk_len == 256 || (ctx->remaining == 0 && ctx->chunk_len > 0)) {
            ctx->lfs->user_data = ctx->chunk;
            const char* fa[26] = {0};
            char nb[12];
            snprintf(nb, sizeof(nb), "%lu", (unsigned long)ctx->chunk_len);
            fa[APPCMD_ARG('n')] = nb;
            int r = appcmd_argv(ctx->lfs, "fwrite", fa);
            ctx->lfs->user_data = NULL;
            ctx->chunk_len = 0;
            if (r < 0) { ctx->active = 0; return -1; }
        }
    }

    if (ctx->remaining == 0) {
        int cr = appcmd_argv(ctx->lfs, "close", NULL);
        (void)cr;
        uint8_t done = '.';
        appwrite(ksc_console, &done, 1, 0x11);
        ctx->active = 0;
    }

    return (int)consumed;
}

static int upload_open(app_t* app)
{
    upload_ctx_t* ctx = (upload_ctx_t*)osmalloc(sizeof(upload_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(upload_ctx_t));
    ctx->lfs = app->app0;
    app->app_data = ctx;
    return 0;
}

static int upload_close(app_t* app)
{
    upload_ctx_t* ctx = (upload_ctx_t*)app->app_data;
    if (ctx) osfree(ctx);
    app->app_data = NULL;
    return 0;
}

static int upload_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static const papp_ops_t upload_ops = {
    .open  = upload_open,
    .close = upload_close,
    .read  = upload_read,
    .write = upload_write,
    .cmd   = upload_cmd,
};

REGISTER_APP_EX("upload", NULL, "1\0littlefs", &upload_ops,
    "Binary file upload via UART to littlefs");

#endif
