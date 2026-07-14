#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "../inc/kscgui.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__

#define TERM_WIDTH      240
#define TERM_HEIGHT     240
#define HEADER_H        20
#define TEXTLINE_H      7
#define TEXTADVANCE_W   7

#define TERM_LINE_BUF  128

typedef struct {
    uint16_t   cx, cy;
    KSCCOLOR   fg;
    app_t*     gui;
    tile_h_t   tile;
    app_t*     uart;
    app_t*     cmd;
    char       line[TERM_LINE_BUF];
    uint8_t    pos;
} term_ctx_t;

static int term_open(app_t* app)
{
    if (app->app_data) return 0;
    term_ctx_t* ctx = (term_ctx_t*)osmalloc(sizeof(term_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(term_ctx_t));
    ctx->gui = app->app0;
    appopen(ctx->gui);
    ctx->uart = appget("uart_serial");
    if (ctx->uart) appopen(ctx->uart);

    ctx->cmd = appget("cmd");
    if (ctx->cmd) appopen(ctx->cmd);

    GUI_SETSPI(ctx->gui, 2);
    GUI_INIT(ctx->gui);

    ctx->tile = GUI_WCREATE(ctx->gui, 0, 0, TERM_WIDTH, TERM_HEIGHT, bblack);
    GUI_WSELECT(ctx->gui, ctx->tile);
    GUI_WCLEAR(ctx->gui);

    GUI_FILL(ctx->gui, 0, 0, TERM_WIDTH, HEADER_H, bblue);
    GUI_STRING(ctx->gui, 4, 6, "TERMINAL", wwhite, bblue);
    GUI_RECT(ctx->gui, 0, 0, TERM_WIDTH, TERM_HEIGHT, bblue);

    ctx->fg = wwhite;
    ctx->cy = HEADER_H;
    app->app_data = ctx;
    return 0;
}

static int term_close(app_t* app)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (ctx) {
        if (ctx->gui) appclose(ctx->gui);
        if (ctx->uart) appclose(ctx->uart);
        if (ctx->cmd) appclose(ctx->cmd);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static void term_putchar(term_ctx_t* ctx, char ch)
{
    switch (ch) {
    case '\r':
        ctx->cx = 0;
        return;
    case '\n':
        ctx->cx = 0;
        ctx->cy += TEXTLINE_H;
        if (ctx->cy + TEXTLINE_H > TERM_HEIGHT)
            ctx->cy = HEADER_H;
        return;
    case '\b':
        if (ctx->cx >= TEXTADVANCE_W) {
            ctx->cx -= TEXTADVANCE_W;
            GUI_FILL(ctx->gui, ctx->cx, ctx->cy, TEXTADVANCE_W, TEXTLINE_H, bblack);
        }
        return;
    default:
        if ((uint8_t)ch < ' ') return;
        break;
    }

    GUI_CHAR(ctx->gui, ctx->cx, ctx->cy, ch, ctx->fg, bblack);
    ctx->cx += TEXTADVANCE_W;
    if (ctx->cx + TEXTADVANCE_W > TERM_WIDTH) {
        ctx->cx = 0;
        ctx->cy += TEXTLINE_H;
        if (ctx->cy + TEXTLINE_H > TERM_HEIGHT)
            ctx->cy = HEADER_H;
    }
}

static void term_cls(term_ctx_t* ctx)
{
    GUI_FILL(ctx->gui, 0, HEADER_H, TERM_WIDTH, TERM_HEIGHT - HEADER_H, bblack);
    ctx->cx = 0;
    ctx->cy = HEADER_H;
}

static int term_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (!ctx || !data) return 0;

    if (ctx->uart)
        appwrite(ctx->uart, data, count, 0x11);

    switch (mode & 0x0F) {
    case 0x01:
    {
        uint8_t* p = (uint8_t*)data;
        for (uint32_t i = 0; i < count; i++)
            term_putchar(ctx, (char)p[i]);
        return (int)count;
    }
    case 0x02:
        term_cls(ctx);
        return 1;
    }
    return 0;
}

static int term_poll(term_ctx_t* ctx)
{
    if (!ctx->uart) return 0;
    uint8_t buf[64];
    int n = appread(ctx->uart, buf, sizeof(buf), 0x11);
    for (int i = 0; i < n; i++) {
        uint8_t ch = buf[i];
        if (ch == '\r' || ch == '\n') {
            if (ctx->pos == 0) continue;
            ctx->line[ctx->pos] = '\0';
            appwrite(ctx->uart, "\r\n", 2, 0x11);
            term_putchar(ctx, '\r');
            term_putchar(ctx, '\n');
            if (ctx->cmd)
                appioctl(ctx->cmd, "exec", ctx->line);
            ctx->pos = 0;
        } else if (ch == '\b' || ch == 127) {
            if (ctx->pos > 0) {
                ctx->pos--;
                appwrite(ctx->uart, "\b \b", 3, 0x11);
                term_putchar(ctx, '\b');
            }
        } else if (ctx->pos < TERM_LINE_BUF - 1) {
            ctx->line[ctx->pos++] = (char)ch;
            appwrite(ctx->uart, &ch, 1, 0x11);
            term_putchar(ctx, (char)ch);
        }
    }
    return n;
}

static int term_ioctl(app_t* app, const char* cmd, va_list ap)
{
    term_ctx_t* ctx = (term_ctx_t*)app->app_data;
    if (!ctx) return -1;

    if (strcmp(cmd, "cls") == 0) {
        term_cls(ctx);
        return 1;
    }

    if (strcmp(cmd, "poll") == 0)
        return term_poll(ctx);

    char buf[128];
    int n = vsnprintf(buf, sizeof(buf), cmd, ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        if (ctx->uart)
            appwrite(ctx->uart, buf, len, 0x11);
        for (size_t i = 0; i < len; i++)
            term_putchar(ctx, buf[i]);
    }
    return n;
}

static const papp_ops_t term_ops = {
    .open  = term_open,
    .close = term_close,
    .write = term_write,
    .ioctl = term_ioctl,
};

REGISTER_APP_EX("term", "0", "1\0KSCGUI", &term_ops,
    "LCD terminal — zero-buffer fputc rendering");

#endif
