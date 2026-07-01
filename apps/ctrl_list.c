#include "../inc/app.h"
#include "../inc/ctrl_list.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>

#if __USE_STM32__

#define CTRL_INTERVAL_MS    50
#define HOLD_TICKS          10
#define HOLD_GAP            4

enum { KEY_PRESS = 0, KEY_RELEASE = 1, KEY_HOLD = 2 };

typedef struct {
    app_t*      app;
    app_t*      list;
    app_t*      kpd;
    app_t*      tim;

    ctrl_keymap_t km;

    ctrl_event_cb_t callback;
    void*           user_data;

    uint16_t interval_ms;
    uint8_t  hw_opened;
} ctrl_ctx_t;

static const ctrl_keymap_t default_keymap = {
    .up   = 6,
    .down = 14,
    .ok   = 10,
    .quit = 0,
};

static void* tick_cb(void* data)
{
    app_t* app = (app_t*)data;
    ctrl_ctx_t* ctx = (ctrl_ctx_t*)app->app_data;
    if (!ctx) return NULL;

    uint32_t ev;
    while (appread(ctx->kpd, &ev, 0, 3) > 0) {
        uint8_t key  = (ev >> 4) & 0xF;
        uint8_t type = ev & 0xF;

        if (key == ctx->km.up || key == ctx->km.down) {
            if (type == KEY_PRESS || type == KEY_HOLD) {
                int delta = (key == ctx->km.up) ? -1 : 1;
                appioctl(ctx->list, "move", delta);
            }
        } else if (type == KEY_PRESS) {
            if (key == ctx->km.ok) {
                appioctl(ctx->list, "confirm");
                if (ctx->callback)
                    ctx->callback(ctx->user_data, CTRL_EVENT_CONFIRM);
            } else if (key == ctx->km.quit) {
                if (ctx->callback)
                    ctx->callback(ctx->user_data, CTRL_EVENT_QUIT);
            }
        }
    }

    return NULL;
}

/* ── lifecycle ── */

static int ctrl_open(app_t* app)
{
    ctrl_ctx_t* ctx = (ctrl_ctx_t*)osmalloc(sizeof(ctrl_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(ctrl_ctx_t));

    ctx->app  = app;
    ctx->list = app->app0;
    ctx->kpd  = app->app1;
    ctx->tim  = appget("tim_clock");

    ctx->interval_ms = CTRL_INTERVAL_MS;
    ctx->km = default_keymap;

    app->app_data = ctx;
    return 0;
}

static int ctrl_close(app_t* app)
{
    ctrl_ctx_t* ctx = (ctrl_ctx_t*)app->app_data;
    if (!ctx) return 0;
    if (ctx->hw_opened) {
        appwrite(ctx->tim, NULL, 0, 0x42);
        appclose(ctx->kpd);
        appclose(ctx->list);
    }
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

/* ── handlers ── */

static int handler_init(ctrl_ctx_t* ctx, va_list ap)
{
    (void)ap;
    if (ctx->hw_opened) return -1;

    if (appopen(ctx->kpd) < 0) return -1;
    appwrite(ctx->kpd, NULL, 0, 1);
    { uint32_t iv = ctx->interval_ms; appwrite(ctx->kpd, &iv, 1, 2); }
    { uint32_t p[2] = {HOLD_TICKS, HOLD_GAP}; appwrite(ctx->kpd, p, 2, 5); }

    if (appopen(ctx->list) < 0) { appclose(ctx->kpd); return -1; }

    ctx->tim->callback  = tick_cb;
    ctx->tim->user_data = ctx->app;
    appopen(ctx->tim);
    appwrite(ctx->tim, NULL, ctx->interval_ms, 0x41);
    appwrite(ctx->tim, NULL, 1,              0x42);

    ctx->hw_opened = 1;
    return 1;
}

static int handler_setmap(ctrl_ctx_t* ctx, va_list ap)
{
    const ctrl_keymap_t* km = va_arg(ap, const ctrl_keymap_t*);
    ctx->km = *km;
    return 1;
}

static int handler_setinterval(ctrl_ctx_t* ctx, va_list ap)
{
    int ms = va_arg(ap, int);
    if (ms < 10) ms = 10;
    ctx->interval_ms = (uint16_t)ms;
    if (ctx->hw_opened)
        appwrite(ctx->tim, NULL, ctx->interval_ms, 0x41);
    return 1;
}

static int handler_setcb(ctrl_ctx_t* ctx, va_list ap)
{
    ctx->callback  = va_arg(ap, ctrl_event_cb_t);
    ctx->user_data = va_arg(ap, void*);
    return 1;
}

static int handler_getlist(ctrl_ctx_t* ctx, va_list ap)
{
    app_t** out = va_arg(ap, app_t**);
    *out = ctx->list;
    return 1;
}

/* ── ioctl dispatch ── */

typedef int (*ctrl_handler_t)(ctrl_ctx_t*, va_list);

typedef struct {
    const char*    name;
    ctrl_handler_t handler;
} ctrl_cmd_t;

static const ctrl_cmd_t cmd_table[] = {
    {"init",        handler_init},
    {"setmap",      handler_setmap},
    {"setinterval", handler_setinterval},
    {"setcb",       handler_setcb},
    {"getlist",     handler_getlist},
};

static int ctrl_ioctl(app_t* app, const char* fmt, va_list ap)
{
    ctrl_ctx_t* ctx = (ctrl_ctx_t*)app->app_data;
    if (!ctx) return -1;

    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); i++) {
        if (strcmp(fmt, cmd_table[i].name) == 0)
            return cmd_table[i].handler(ctx, ap);
    }
    return 0;
}

static int ctrl_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    ctrl_ctx_t* ctx = (ctrl_ctx_t*)app->app_data;
    if (!ctx || !data) return 0;
    if (mode == 1) {
        *(uint32_t*)data = ctx->hw_opened ? 1 : 0;
        return 4;
    }
    return 0;
}

static int ctrl_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static const papp_ops_t ctrl_ops = {
    .open  = ctrl_open,
    .close = ctrl_close,
    .read  = ctrl_read,
    .write = ctrl_write,
    .ioctl = ctrl_ioctl,
};

REGISTER_APP_EX("ctrl_list", "0", "2\0list\0button16",
                &ctrl_ops, "Navigable list (button16 up/down/ok/quit)");

#endif
