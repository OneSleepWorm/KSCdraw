#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__ || __USE_PC__

#define LINE_BUF_SIZE   80

typedef struct {
    char        line_buf[LINE_BUF_SIZE];
    uint8_t     line_len;
    app_t*      console;
} term_ctx_t;

static void term_console_puts(app_t* console, const char* s)
{
    if (!console || !s) return;
    size_t len = strlen(s);
    if (len) appwrite(console, (void*)s, (uint32_t)len, 0x11);
}

static void term_help(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    term_console_puts(ctx->console, "apps:\r\n");
    size_t app_count = ((const char*)__stop_app_table - (const char*)__start_app_table)
                       / sizeof(papp_t);
    char buf[24];
    for (size_t i = 0; i < app_count; i++) {
        const papp_t* p = &__start_app_table[i];
        if (p->base && p->base->app_name) {
            int n = snprintf(buf, sizeof(buf), "  %s\r\n", p->base->app_name);
            if (n > 0) term_console_puts(ctx->console, buf);
        }
    }
}

static int term_echo(const void* data, uint32_t len, void* ctx)
{
    if (!data || !len || !ctx) return -1;
    return appwrite((app_t*)ctx, (void*)data, len, 0x11);
}

static int term_dispatch(app_t* app, const char* cmdline)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (!ctx || !cmdline || !*cmdline) return -1;

    while (*cmdline == ' ') cmdline++;
    if (!*cmdline) return -1;

    const char* p = cmdline;
    while (*p && *p != ' ') p++;
    size_t applen = p - cmdline;
    if (applen == 0 || applen > 31) return -1;

    char appname[32];
    memcpy(appname, cmdline, applen);
    appname[applen] = '\0';

    const char* rest = p;
    while (*rest == ' ') rest++;

    if (strcmp(appname, "help") == 0 || strcmp(appname, "?") == 0) {
        term_help(app);
        return 0;
    }

    app_t* target = appget(appname);
    if (!target) {
        char b[48];
        int n = snprintf(b, sizeof(b), "error: unknown app '%s'\r\n", appname);
        if (n > 0) term_console_puts(ctx->console, b);
        return -1;
    }
    appopen(target);

    int r;
    if (*rest) {
        void* saved_ud = target->user_data;
        app_output_fn saved_ofn = target->output_fn;
        void* saved_octx = target->output_ctx;

        if (app->user_data) {
            target->user_data = app->user_data;
            target->output_fn = NULL;
        } else {
            target->user_data = NULL;
            target->output_fn = term_echo;
            target->output_ctx = ctx->console;
        }

        r = appcmd(target, rest);

        if (r < 0) {
            char b[48];
            int n = snprintf(b, sizeof(b), "error: cmd returned %d\r\n", r);
            if (n > 0) term_console_puts(ctx->console, b);
        }

        target->user_data = saved_ud;
        target->output_fn = saved_ofn;
        target->output_ctx = saved_octx;
    } else {
        r = 0;
    }

    app->user_data = NULL;
    app->mode_data = NULL;

    uint8_t nul = 0;
    appwrite(ctx->console, &nul, 1, 0x11);
    return r;
}

static int term_open(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)osmalloc(sizeof(term_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(term_ctx_t));
    ctx->console = ksc_console;
    app->app_data = ctx;
    return 0;
}

static int term_close(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (ctx) {
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static int term_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static int term_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (!ctx || !data || count == 0) return -1;

    if (mode == 0) {
        uint8_t* bytes = (uint8_t*)data;
        for (uint32_t i = 0; i < count; i++) {
            uint8_t c = bytes[i];
            if (c == '\r' || c == '\n') {
                if (ctx->line_len > 0) {
                    ctx->line_buf[ctx->line_len] = '\0';
                    int r = term_dispatch(app, ctx->line_buf);
                    ctx->line_len = 0;
                    return r;
                }
            } else if (c == '\b' || c == 0x7F) {
                if (ctx->line_len > 0) ctx->line_len--;
            } else if (c == '\0') {
            } else if (ctx->line_len < LINE_BUF_SIZE - 1) {
                ctx->line_buf[ctx->line_len++] = c;
            }
        }
        return (int)count;
    }

    if (mode == 1) {
        uint32_t len = count;
        const char* s = (const char*)data;
        while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n'))
            len--;
        if (len > 0) {
            if (len >= LINE_BUF_SIZE) len = LINE_BUF_SIZE - 1;
            memcpy(ctx->line_buf, s, len);
            ctx->line_buf[len] = '\0';
            return term_dispatch(app, ctx->line_buf);
        }
        return 0;
    }

    return -1;
}

static int term_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)app; (void)cmdname; (void)argv;
    return -1;
}

static const papp_ops_t term_ops = {
    .open  = term_open,
    .close = term_close,
    .read  = term_read,
    .write = term_write,
    .cmd   = term_cmd,
};

REGISTER_APP("terminal", "0", &term_ops, "Command router — dispatch strings to apps");

#endif /* __USE_STM32__ */
