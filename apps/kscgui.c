/**
 * @file    kscgui.c
 * @note    GUI Manager App — 封装 KSCdraw 渲染 + ST7789 驱动
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  KSCGUI
 * 依赖:    super_spi1 + super_spi2 (app 依赖)
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用 (对比: 移除 kscgui.o 后固件尺寸差值, 含拉入的 KSCdraw 代码)
 * ============================================================
 *   ROM(Debug -O0):   11,896 B
 *   ROM(Release -Os): 6,888 B
 *   RAM(静态):  4 B (_ctx 全局指针)
 *   RAM(堆):    gui_ctx_t (~620 B, 含 pixbuf[512])
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("KSCGUI") → app_t*
 *   appopen(gui)          : 打开 super_spi 默认 (SPI2→SPI1), 创建默认全屏窗口
 *   appioctl(gui,"setspi",n) : 切换 SPI 后端 (1=SPI1, 2=SPI2)
 *   appioctl(gui,"init")     : 发送 ST7789 初始化序列
 *   appioctl(gui,"wcreate",x,y,w,h,bk) : 创建窗口 → 返回 id
 *   appioctl(gui,"wdelete",id)         : 删除窗口
 *   appioctl(gui,"wselect",id)         : 设活动窗口
 *   appioctl(gui,"wclear")             : 清除活动窗口
 *   appioctl(gui,"setobjs",n,ptr)      : 注册对象数组
 *   appioctl(gui,"drawobjs",n)         : 三遍脏渲染
 *   appioctl(gui,"drawobj",idx)        : 单个对象脏渲染
 *   绘图: pixel/fill/frect/rect/line/circle/fcircle/arc/
 *         rrect/frrect/char/string/strcn/image/ibig/ibin
 *   appclose(gui) : 关闭 SPI, 释放内存
 *
 * 典型用法:
 *   app_t* gui = appget("KSCGUI");
 *   appopen(gui);
 *   appioctl(gui, "setspi", 2);
 *   appioctl(gui, "init");
 *   int w = appioctl(gui, "wcreate", 0,0,240,320, 0x0000);
 *   appioctl(gui, "wselect", w);
 *   appioctl(gui, "fill", 10,10,50,30, 0xF800);
 *   appclose(gui);
 */

#include "../inc/app.h"
#include "../inc/KSCdraw.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdarg.h>

#if __USE_STM32__

#define TFT_W 240
#define TFT_H 320
#define GUI_MAX_WIN 4

typedef struct {
    KSC_window  scr;
} gui_win_t;

typedef struct {
    app_t*          spi[2];         /* [0]=super_spi1, [1]=super_spi2 */
    app_t*          sspi;           /* currently active SPI */
    uint8_t         sspi_opened;
    k_draw_device   dev;
    gui_win_t       wins[GUI_MAX_WIN];
    uint8_t         win_count;
    uint8_t         active_win;
    uint8_t         pixbuf[512];
    ksc_obj_t* obj_ptr;       /* user-managed object array (setobjs) */
    uint16_t        obj_count;      /* total objects in user array */
} gui_ctx_t;

static gui_ctx_t* _ctx;

/* ================================================================
 * SPI device functions
 * ================================================================ */
static void gui_init_stub(void) { }

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

static void gui_window_setcanvas(k_draw_device* dev, KSC_window* screen,
    uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    (void)dev;
    gui_setcanvas(Gx + screen->ssx, Gy + screen->ssy, width, height);
}

/* ================================================================
 * ST7789 initialization
 * ================================================================ */
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

/* ================================================================
 * App lifecycle
 * ================================================================ */
static int gui_open(app_t* app)
{
    gui_ctx_t* ctx = (gui_ctx_t*)osmalloc(sizeof(gui_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(gui_ctx_t));

    ctx->spi[0] = app->app0;    /* super_spi1 */
    ctx->spi[1] = app->app1;    /* super_spi2 */

    ctx->dev.init = gui_init_stub;
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;

    /* Open default SPI: SPI2 first, fallback to SPI1 */
    if (ctx->spi[1]) ctx->sspi = ctx->spi[1];
    else ctx->sspi = ctx->spi[0];
    if (ctx->sspi) {
        if (appopen(ctx->sspi) < 0) { osfree(ctx); return -1; }
        ctx->sspi_opened = 1;
    }

    /* Default window 0: full screen */
    ctx->wins[0].scr.ssx = 0;
    ctx->wins[0].scr.ssy = 0;
    ctx->wins[0].scr.width = TFT_W;
    ctx->wins[0].scr.height = TFT_H;
    ctx->wins[0].scr.bk = 0;
    ctx->wins[0].scr.Mode = 0x80;
    ctx->wins[0].scr.objbuf = NULL;
    ctx->wins[0].scr.objnum = 0;
    ctx->win_count = 1;
    ctx->active_win = 0;

    app->app_data = ctx;
    return 0;
}

static int gui_close(app_t* app)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    if (ctx) {
        if (ctx->sspi && ctx->sspi_opened) appclose(ctx->sspi);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

/* ================================================================
 * ioctl handler
 * ================================================================ */
static int gui_ioctl(app_t* app, const char* cmd, va_list ap)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    _ctx = ctx;
    KSC_window* scr = &ctx->wins[ctx->active_win].scr;

    /* --- lifecycle --- */
    if (strcmp(cmd, "setspi") == 0) {
        int n = va_arg(ap, int);
        if (n < 1 || n > 2) return 0;
        app_t* new_spi = ctx->spi[n - 1];
        if (!new_spi || new_spi == ctx->sspi) return 0;
        if (ctx->sspi_opened) appclose(ctx->sspi);
        ctx->sspi = new_spi;
        if (appopen(ctx->sspi) < 0) { ctx->sspi = NULL; ctx->sspi_opened = 0; return 0; }
        return 1;
    }

    if (strcmp(cmd, "init") == 0) {
        if (!ctx->sspi || !ctx->sspi_opened) return 0;
        gui_init_st7789();
        return 1;
    }

    if (strcmp(cmd, "orient") == 0) {
        uint8_t mode = (uint8_t)va_arg(ap, int);
        uint8_t mcmd = 0x36;
        appwrite(ctx->sspi, &mcmd, 1, 10);
        appwrite(ctx->sspi, &mode, 1, 11);
        return 1;
    }

    /* --- window management --- */
    if (strcmp(cmd, "wcreate") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        KSCCOLOR bk = (KSCCOLOR)va_arg(ap, int);
        for (uint8_t i = 0; i < GUI_MAX_WIN; i++) {
            if (ctx->wins[i].scr.Mode & 0x80) continue;
            ctx->wins[i].scr.ssx = x;
            ctx->wins[i].scr.ssy = y;
            ctx->wins[i].scr.width = w;
            ctx->wins[i].scr.height = h;
            ctx->wins[i].scr.bk = bk;
            ctx->wins[i].scr.Mode = 0x80;
            ctx->wins[i].scr.objbuf = NULL;
            ctx->wins[i].scr.objnum = 0;
            ctx->win_count++;
            return (int)i;
        }
        return -1;
    }

    if (strcmp(cmd, "wdelete") == 0) {
        int id = va_arg(ap, int);
        if (id < 0 || id >= GUI_MAX_WIN) return 0;
        if (!(ctx->wins[id].scr.Mode & 0x80)) return 0;
        ctx->wins[id].scr.Mode = 0;
        ctx->wins[id].scr.objbuf = NULL;
        ctx->wins[id].scr.objnum = 0;
        ctx->win_count--;
        if (ctx->active_win == id && ctx->win_count > 0) {
            for (uint8_t i = 0; i < GUI_MAX_WIN; i++) {
                if (ctx->wins[i].scr.Mode & 0x80) {
                    ctx->active_win = i;
                    break;
                }
            }
        }
        return 1;
    }

    if (strcmp(cmd, "wselect") == 0) {
        int id = va_arg(ap, int);
        if (id < 0 || id >= GUI_MAX_WIN) return 0;
        if (!(ctx->wins[id].scr.Mode & 0x80)) return 0;
        ctx->active_win = (uint8_t)id;
        return 1;
    }

    if (strcmp(cmd, "wclear") == 0) {
        kfull(&ctx->dev, scr, scr->bk, 0, 0, scr->width, scr->height);
        return 1;
    }

    /* --- drawing primitives (active window) --- */
    if (strcmp(cmd, "clear") == 0) {
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfull(&ctx->dev, scr, c, 0, 0, scr->width, scr->height);
        return 1;
    }

    if (strcmp(cmd, "pixel") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        ksetpixel(&ctx->dev, scr, c, x, y);
        return 1;
    }

    if (strcmp(cmd, "fill") == 0 || strcmp(cmd, "frect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfull(&ctx->dev, scr, c, x, y, w, h);
        return 1;
    }

    if (strcmp(cmd, "rect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kbox(&ctx->dev, scr, c, x, y, w, h);
        return 1;
    }

    if (strcmp(cmd, "circle") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kcircle(&ctx->dev, scr, c, x, y, r);
        return 1;
    }

    if (strcmp(cmd, "fcircle") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfillcircle(&ctx->dev, scr, c, x, y, r);
        return 1;
    }

    if (strcmp(cmd, "line") == 0) {
        uint16_t x0 = (uint16_t)va_arg(ap, int);
        uint16_t y0 = (uint16_t)va_arg(ap, int);
        uint16_t x1 = (uint16_t)va_arg(ap, int);
        uint16_t y1 = (uint16_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kline(&ctx->dev, scr, c, x0, y0, x1, y1);
        return 1;
    }

    if (strcmp(cmd, "arc") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        uint8_t  d = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        karc(&ctx->dev, scr, c, x, y, r, d);
        return 1;
    }

    if (strcmp(cmd, "rrect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kroundrect(&ctx->dev, scr, c, x, y, w, h, r);
        return 1;
    }

    if (strcmp(cmd, "frrect") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint16_t w = (uint16_t)va_arg(ap, int);
        uint16_t h = (uint16_t)va_arg(ap, int);
        uint8_t  r = (uint8_t)va_arg(ap, int);
        KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
        kfillroundrect(&ctx->dev, scr, c, x, y, w, h, r);
        return 1;
    }

    if (strcmp(cmd, "char") == 0) {
        uint16_t x  = (uint16_t)va_arg(ap, int);
        uint16_t y  = (uint16_t)va_arg(ap, int);
        char     ch = (char)va_arg(ap, int);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kchar(&ctx->dev, scr, ch, x, y, fg, bg);
        return 1;
    }

    if (strcmp(cmd, "string") == 0) {
        uint16_t x  = (uint16_t)va_arg(ap, int);
        uint16_t y  = (uint16_t)va_arg(ap, int);
        const char* s = va_arg(ap, const char*);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kstring(&ctx->dev, scr, s, x, y, fg, bg);
        return 1;
    }

#if __USE_CHINESE__
    if (strcmp(cmd, "strcn") == 0) {
        uint16_t x  = (uint16_t)va_arg(ap, int);
        uint16_t y  = (uint16_t)va_arg(ap, int);
        const char* s = va_arg(ap, const char*);
        KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
        KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
        kstringchinese(&ctx->dev, scr, s, x, y, fg, bg);
        return 1;
    }
#endif

    /* --- image (direct SPI fast-path) --- */
    if (strcmp(cmd, "image") == 0) {
        uint16_t x = (uint16_t)va_arg(ap, int);
        uint16_t y = (uint16_t)va_arg(ap, int);
        uint8_t  w = (uint8_t)va_arg(ap, int);
        uint8_t  h = (uint8_t)va_arg(ap, int);
        const uint8_t* img = va_arg(ap, const uint8_t*);
        gui_window_setcanvas(&ctx->dev, scr, x, y, w, h);
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
                kfull(&ctx->dev, scr, c, x + ww * s, y + hh * s, s, s);
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
        kimagebin(&ctx->dev, scr, img, x, y, w, h, fg, bg);
        return 1;
    }

    /* --- object system --- */
    if (strcmp(cmd, "setobjs") == 0) {
        uint16_t n = (uint16_t)va_arg(ap, int);
        ksc_obj_t* objs = va_arg(ap, ksc_obj_t*);
        ctx->obj_count = n;
        ctx->obj_ptr = objs;
        return 1;
    }

    if (strcmp(cmd, "drawobjs") == 0) {
        uint16_t n = (uint16_t)va_arg(ap, int);
        if (!ctx->obj_ptr || ctx->obj_count == 0 || n == 0) return 0;
        if (n > ctx->obj_count) n = ctx->obj_count;
        for (uint16_t i = 0; i < n; i++) {
            ksc_obj_t* obj = &ctx->obj_ptr[i];
            if ((obj->_type & (_active|_dirty)) == (_active|_dirty)) {
                kfull(&ctx->dev, scr, scr->bk, obj->sdx, obj->sdy, obj->width, obj->height);
            }
        }
        for (uint16_t i = 0; i < n; i++) {
            ksc_obj_t* obj = &ctx->obj_ptr[i];
            if ((obj->_type & (_active|_visible)) != (_active|_visible)) continue;
            kobjdraw(&ctx->dev, scr, obj);
        }
        for (uint16_t i = 0; i < n; i++) {
            ctx->obj_ptr[i]._type &= ~_dirty;
        }
        return 1;
    }

    if (strcmp(cmd, "drawobj") == 0) {
        uint16_t idx = (uint16_t)va_arg(ap, int);
        if (!ctx->obj_ptr || idx >= ctx->obj_count) return 0;
        ksc_obj_t* obj = &ctx->obj_ptr[idx];
        if (obj->_type & _dirty) {
            kfull(&ctx->dev, scr, scr->bk, obj->sdx, obj->sdy, obj->width, obj->height);
            obj->_type &= ~_dirty;
        }
        if ((obj->_type & (_active|_visible)) == (_active|_visible)) {
            kobjdraw(&ctx->dev, scr, obj);
        }
        return 1;
    }

    return 0;
}

/* ================================================================
 * App descriptor
 * ================================================================ */
static const papp_ops_t kscgui_ops = {
    .open  = gui_open,
    .close = gui_close,
    .ioctl = gui_ioctl,
};

/* Dependencies: super_spi1 (app0), super_spi2 (app1) */
REGISTER_APP_EX("KSCGUI", "0", "2\0super_spi1\0super_spi2", &kscgui_ops,
    "KSC GUI Manager (SPI1/SPI2, multi-window, object rendering)");

#endif
