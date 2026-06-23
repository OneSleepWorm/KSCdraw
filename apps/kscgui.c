#include "../inc/app.h"
#include "../inc/KSCdraw.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdarg.h>

#if __USE_STM32__

#define TFT_W 240
#define TFT_H 320

typedef struct {
    app_t*         sspi;
    k_draw_device  dev;
    KSC_window     scr;
    uint8_t        pixbuf[512];
} gui_ctx_t;

static gui_ctx_t* _ctx;

static void gui_setcanvas(uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    app_t* sspi = _ctx->sspi;
    uint16_t ex = Gx + width - 1;
    uint16_t ey = Gy + height - 1;
    uint8_t ca[] = {Gx >> 8, Gx & 0xFF, ex >> 8, ex & 0xFF};
    uint8_t ra[] = {Gy >> 8, Gy & 0xFF, ey >> 8, ey & 0xFF};
    uint8_t cmd;
    cmd = 0x2A; appwrite(sspi, &cmd, 1, 10);
    appwrite(sspi, ca, 4, 11);
    cmd = 0x2B; appwrite(sspi, &cmd, 1, 10);
    appwrite(sspi, ra, 4, 11);
    cmd = 0x2C; appwrite(sspi, &cmd, 1, 10);
}

static void gui_pixels(const KSCCOLOR* colors, uint16_t num)
{
    app_t* sspi = _ctx->sspi;
    uint8_t* pixbuf = _ctx->pixbuf;
    uint16_t batch = sizeof(_ctx->pixbuf) / 2;
    while (num) {
        uint16_t n = (num > batch) ? batch : num;
        uint8_t* p = pixbuf;
        for (uint16_t i = 0; i < n; i++) {
            KSCCOLOR c = colors[i];
            *p++ = c >> 8;
            *p++ = c & 0xFF;
        }
        appwrite(sspi, pixbuf, n * 2, 13);
        colors += n;
        num -= n;
    }
}

static void gui_window_setcanvas(k_draw_device* dev, KSC_window* screen, uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    (void)dev;
    gui_setcanvas(Gx + screen->ssx, Gy + screen->ssy, width, height);
}

static int gui_open(app_t* app)
{
    gui_ctx_t* ctx = (gui_ctx_t*)osmalloc(sizeof(gui_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(gui_ctx_t));
    ctx->sspi = app->app0;
    if (appopen(ctx->sspi) < 0) { osfree(ctx); return -1; }
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;
    ctx->scr.ssx = 0;
    ctx->scr.ssy = 0;
    ctx->scr.width = TFT_W;
    ctx->scr.height = TFT_H;
    ctx->scr.bk = 0;
    ctx->scr.Mode = 0;
    ctx->scr.objbuf = NULL;
    ctx->scr.objnum = 0;
    ctx->scr.dirty_rect_buf = NULL;
    ctx->scr.dirty_rect_num = 0;
    app->app_data = ctx;
    return 0;
}

static int gui_close(app_t* app)
{
    if (app->app_data) {
        osfree(app->app_data);
        app->app_data = NULL;
    }
    return 0;
}

static void gui_init_st7789(void)
{
    app_t* sspi = _ctx->sspi;
    appwrite(sspi, NULL, 0, 14);
    static const uint8_t init[] = {
        0x11,0,  0x00,0,  0x3A,1,0x05,  0xC5,1,0x1A,
        0x36,1,0x00,  0xB2,5,0x05,0x05,0x00,0x33,0x33,
        0xB7,1,0x05,  0xBB,1,0x3F,  0xC0,1,0x2C,
        0xC2,1,0x01,  0xC3,1,0x0F,  0xC4,1,0x20,
        0xC6,1,0x01,  0xD0,2,0xA4,0xA1,
        0xE8,1,0x03,  0xE9,3,0x09,0x09,0x08,
        0xE0,14,0xD0,0x05,0x09,0x09,0x08,0x14,0x28,0x33,0x3F,0x07,0x13,0x14,0x28,0x30,
        0xE1,14,0xD0,0x05,0x09,0x09,0x08,0x03,0x24,0x32,0x32,0x3B,0x14,0x13,0x28,0x2F,
        0x20,0,  0x00,0,  0x29,0,  0xFF
    };
    const uint8_t* p = init;
    while (*p != 0xFF) {
        uint8_t cmd = *p++;
        uint8_t n = *p++;
        if (cmd == 0x00) { sysdelay(120); continue; }
        appwrite(sspi, &cmd, 1, 10);
        if (n) appwrite(sspi, (void*)p, n, 11);
        p += n;
    }
}

static int gui_ioctl(app_t* app, const char* cmd, va_list ap)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    _ctx = ctx;
    if (strcmp(cmd, "init") == 0) {
        gui_init_st7789();
        return 1;
    }
    if (strcmp(cmd, "clear") == 0) {
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfull(&ctx->dev, &ctx->scr, c, 0, 0, TFT_W, TFT_H);
        return 1;
    }
    if (strcmp(cmd, "pixel") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        ksetpixel(&ctx->dev, &ctx->scr, c, x, y);
        return 1;
    }
    if (strcmp(cmd, "fill") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfull(&ctx->dev, &ctx->scr, c, x, y, w, h);
        return 1;
    }
    if (strcmp(cmd, "rect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kbox(&ctx->dev, &ctx->scr, c, x, y, w, h);
        return 1;
    }
    if (strcmp(cmd, "circle") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kcircle(&ctx->dev, &ctx->scr, c, x, y, r);
        return 1;
    }
    if (strcmp(cmd, "fcircle") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfillcircle(&ctx->dev, &ctx->scr, c, x, y, r);
        return 1;
    }
    if (strcmp(cmd, "line") == 0) {
        uint16_t x0 = (uint16_t)va_arg(ap, int);
        uint16_t y0 = (uint16_t)va_arg(ap, int);
        uint16_t x1 = (uint16_t)va_arg(ap, int);
        uint16_t y1 = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kline(&ctx->dev, &ctx->scr, c, x0, y0, x1, y1);
        return 1;
    }
    if (strcmp(cmd, "arc") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        uint8_t  d = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        karc(&ctx->dev, &ctx->scr, c, x, y, r, d);
        return 1;
    }
    if (strcmp(cmd, "rrect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kroundrect(&ctx->dev, &ctx->scr, c, x, y, w, h, r);
        return 1;
    }
    if (strcmp(cmd, "frrect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfillroundrect(&ctx->dev, &ctx->scr, c, x, y, w, h, r);
        return 1;
    }
    if (strcmp(cmd, "char") == 0) {
        uint16_t x  = (uint16_t)va_arg(ap, int);
        uint16_t y  = (uint16_t)va_arg(ap, int);
        char     ch = (char)va_arg(ap, int);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kchar(&ctx->dev, &ctx->scr, ch, x, y, fg, bg);
        return 1;
    }
    if (strcmp(cmd, "string") == 0) {
        uint16_t x  = (uint16_t)va_arg(ap, int);
        uint16_t y  = (uint16_t)va_arg(ap, int);
        const char* s = va_arg(ap, const char*);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kstring(&ctx->dev, &ctx->scr, s, x, y, fg, bg);
        return 1;
    }
    if (strcmp(cmd, "image") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  w = (uint8_t)va_arg(ap, int);
        uint8_t  h = (uint8_t)va_arg(ap, int);
        const uint8_t* img = va_arg(ap, const uint8_t*);
        gui_window_setcanvas(&ctx->dev, &ctx->scr, x, y, w, h);
        uint16_t remain = w * h * 2;
        while (remain) {
            uint16_t n = (remain > sizeof(ctx->pixbuf)) ? (uint16_t)sizeof(ctx->pixbuf) : remain;
            memcpy(ctx->pixbuf, img, n);
            appwrite(ctx->sspi, ctx->pixbuf, n, 13);
            img += n;
            remain -= n;
        }
        return 1;
    }
    if (strcmp(cmd, "ibig") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  w = (uint8_t)va_arg(ap, int);
        uint8_t  h = (uint8_t)va_arg(ap, int);
        uint8_t  s = (uint8_t)va_arg(ap, int);
        const uint8_t* img = va_arg(ap, const uint8_t*);
        for (uint8_t hh = 0; hh < h; hh++) {
            for (uint8_t ww = 0; ww < w; ww++) {
                KSCCOLOR c = ((KSCCOLOR)img[0] << 8) | img[1];
                img += 2;
                kfull(&ctx->dev, &ctx->scr, c, x + ww * s, y + hh * s, s, s);
            }
        }
        return 1;
    }
    if (strcmp(cmd, "ibin") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  w = (uint8_t)va_arg(ap, int);
        uint8_t  h = (uint8_t)va_arg(ap, int);
        const uint8_t* img = va_arg(ap, const uint8_t*);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kimagebin(&ctx->dev, &ctx->scr, img, x, y, w, h, fg, bg);
        return 1;
    }
    if (strcmp(cmd, "orient") == 0) {
        uint8_t mode = (uint8_t)va_arg(ap, int);
        uint8_t mcmd = 0x36;
        appwrite(ctx->sspi, &mcmd, 1, 10);
        appwrite(ctx->sspi, &mode, 1, 11);
        return 1;
    }
    return 0;
}

static const papp_ops_t kscgui_ops = {
    .open  = gui_open,
    .close = gui_close,
    .ioctl = gui_ioctl,
};

REGISTER_APP_EX("KSCGUI2", "0", "1\0super_spi2", &kscgui_ops, "KSC GUI via super_spi2");

#endif
