#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__ || __USE_PC__

#define LINE_BUF_SIZE   80

typedef struct {
    char        line_buf[LINE_BUF_SIZE];
    char*       line_end;
    // char*       cmdend;
    app_t*      console;
} term_ctx_t;

static int term_console_puts(const void* data, uint32_t len, void* ctx)
{
    app_t* console = (app_t*)ctx;
    if (!console || !data) return -1;
    return appwrite(console, (void*)data, len, 0);
}
static void term_help(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    term_console_puts("apps:\r\n", 6, ctx->console);
    size_t app_count = ((const char*)__stop_app_table - (const char*)__start_app_table)
                       / sizeof(papp_t);
    char buf[24];
    for (size_t i = 0; i < app_count; i++) {
        const papp_t* p = &__start_app_table[i];
        if (p->base && p->base->app_name) {
            int n = snprintf(buf, sizeof(buf), "  %s\r\n", p->base->app_name);
            if (n > 0) term_console_puts(buf, (uint32_t)n, ctx->console);
        }
    }
}

static int term_docmd(app_t* term_app, char* cmdline)
{
    term_ctx_t* ctx = (term_ctx_t*)term_app->app_data;
    char* cmdend = strchr(cmdline, ' ');
    if (cmdend) *cmdend++ = '\0';

    if (!strcmp(cmdline, "help") || !strcmp(cmdline, "?")) {
        term_help(term_app);
        return 0;
    }

    app_t* target = appget(cmdline);
    if (!target) return -1;
    appopen(target);

    void*       saved_ud   = target->input_data;
    app_output_fn saved_fn = target->output_fn;
    void*       saved_ctx  = target->output_ctx;

    target->mode_data = term_app->mode_data;

    if (term_app->input_data) {
        target->input_data = term_app->input_data;
        target->output_fn  = NULL;
    } else {
        target->input_data = NULL;
        target->output_fn  = term_console_puts;
        target->output_ctx = ctx->console;
    }

    int ret = appcmd(target, cmdend);

    term_app->output_data = target->output_data;

    target->input_data  = saved_ud;
    target->output_fn   = saved_fn;
    target->output_ctx  = saved_ctx;

    term_app->input_data = NULL;
    term_app->mode_data  = NULL;

    {
        char buf[15];
        int n = snprintf(buf, sizeof(buf), ret >= 0 ? "ok:%d\r\n" : "error:%d\r\n", ret);
        if (n > 0) term_console_puts(buf, (uint32_t)n, ctx->console);
    }
    return ret;
}

static void term_editor(app_t* app, char ch)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    switch(ch) {
    case '\r':
    case '\n':
        *ctx->line_end = '\0';
        term_docmd(app, ctx->line_buf);
        ctx->line_end = ctx->line_buf;
        break;
    case '\b':
        if (ctx->line_end > ctx->line_buf) ctx->line_end--;
        break;
    default:
        if (ctx->line_end < ctx->line_buf + LINE_BUF_SIZE - 1)
            *ctx->line_end++ = ch;
        break;
    }
}

static int term_open(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)osmalloc(sizeof(term_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(term_ctx_t));
    ctx->line_end = ctx->line_buf;
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
    if (!ctx || !data) return -1;

    uint8_t* p = (uint8_t*)data;
    for (uint32_t i = 0; i < count; i++)
        term_editor(app, (char)p[i]);
    return (int)count;
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
