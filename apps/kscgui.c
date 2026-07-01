/**
 * @file    kscgui.c
 * @note    GUI 管理器 — Tile 合成器 + ST7789 驱动
 *
 * ============================================================
 * 架构
 * ============================================================
 *   屏幕划分为独立矩形 tile，每个 tile 有位置/大小/Z 序。
 *   Tile 按 Z 序渲染（高 Z 在上层），支持按 tile 句柄单刷或全刷。
 *
 *   句柄系统：
 *     tile_h_t = [4-bit generation | 4-bit slot index]
 *     generation 防止槽位复用后的 use-after-free，0 = 无效句柄。
 *
 * ============================================================
 * 对象系统 (ksc_obj_t)
 * ============================================================
 *   ksc_obj_t 是纯数据容器——kscgui 不读取/解释任何字段语义。
 *   _type 高 4 位、data、d_and_r、width/height 全部由用户自由定义。
 *
 *   _type 低 4 位 = draw_table[16] 索引：
 *     [0]  fillbox     [4]  image       [8]  fillcircle  [12] char
 *     [1]  box         [5]  imagebig    [9]  arc         [13] 用户
 *     [2]  line        [6]  ibin        [10] roundrect   [14] 用户
 *     [3]  string      [7]  circle      [11] fillrrect   [15] 用户
 *
 *   用户通过 GUI_SETDRAWFUNC 覆盖 13-15 槽（也可覆盖内置槽）。
 *
 *   objbuf 内存由用户管理（static/osmalloc 等），通过 GUI_SETOBJPOOL
 *   注册到 tile。kscgui 不分配/释放 obj 内存。
 *
 * ============================================================
 * 双层 API 模式
 * ============================================================
 *   高频（渲染）：appwrite，不经 strcmp 分发表
 *     mode 0x01 → DRAWOBJ  (单 ksc_obj_t)
 *     mode 0x02 → DRAWOBJS (ksc_obj_t 数组)
 *
 *   低频（配置/管理）：appioctl，经 strcmp 查表
 *     tile 生命周期: wcreate/wdelete/wselect/whide/…
 *     绘图命令: clear/fill/pixel/line/rect/string/…
 *     obj 池: setobjpool/getobjpool
 *     绘制函数槽: setdrawfunc
 *
 * ============================================================
 * 注册名:    KSCGUI
 * 依赖:      super_spi
 * 平台:      STM32 (__USE_STM32__)
 * ============================================================
 *
 * ============================================================
 * 资源占用（LTO 差分法）
 * ============================================================
 *   ROM(Debug -O0):   14,100 B
 *   ROM(Release -Os):  7,392 B
 *   RAM(static):   0 B
 *   RAM(heap):     ~2 KB (gui_ctx_t + 16 tiles + KSC_window, osmalloc)
 *
 * ============================================================
 * 外部 API:
 *   appget("KSCGUI") → app_t*
 *   appopen(gui)
 *   appioctl(gui, "wcreate", x,y,w,h,bk)   → tile_h_t as int
 *   appioctl(gui, "wdelete", handle)
 *   appioctl(gui, "wselect", handle)
 *   完整宏定义见 kscgui.h
 *   appclose(gui)
 */

#include "../inc/app.h"
#include "../inc/kscgui.h"
#include "../inc/super_spi.h"
#include "../inc/KSCdraw.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdarg.h>

#if __USE_STM32__

/* ================================================================
 * Constants
 * ================================================================ */
#define TFT_W           240
#define TFT_H           320
#define TILE_MAX        16

/* Handle encoding helpers */
#define TILE_SLOT(h)            ((h) & 0x0F)
#define TILE_GEN(h)             ((h) >> 4)
#define TILE_MAKE_HANDLE(g,s)   ((tile_h_t)(((uint8_t)(g) << 4) | (uint8_t)(s)))

/* Tile flags */
#define TILE_F_USED     0x01
#define TILE_F_VISIBLE  0x02

/* Command descriptor flags */
#define CMD_NEEDS_TILE  0x01

/* ================================================================
 * Types
 * ================================================================ */
typedef struct {
    KSC_window  win;         /* ssx/ssy/width/height/bk/objbuf/objnum/Mode */
    uint8_t     gen;         /* generation 1..15, 0=slot free */
    uint8_t     flags;       /* TILE_F_USED | TILE_F_VISIBLE */
    uint8_t     z;           /* Z-order, higher = drawn later = on top */
} tile_t;

typedef struct {
    app_t*          sspi;           /* super_spi (unified) */
    int             sspi_inst;      /* active SPI instance: 1 or 2 */
    int             sspi_dev;       /* device ID on active instance */
    int             spi_dev[2];     /* [0]=SPI1 dev, [1]=SPI2 dev */
    k_draw_device   dev;
    tile_t          tiles[TILE_MAX];
    uint16_t        tile_free_map;  /* bitmap: 1=slot free */
    tile_h_t        active_handle;  /* 0=none */
    uint8_t         active_slot;    /* cache of active tile's slot index */
    uint8_t         pixbuf[512];
} gui_ctx_t;

typedef int (*cmd_handler_t)(gui_ctx_t*, KSC_window*, va_list);

typedef struct {
    const char*     name;
    cmd_handler_t   handler;
    uint8_t         flags;          /* CMD_NEEDS_TILE ... */
} cmd_entry_t;

/* ================================================================
 * Internal helpers
 * ================================================================ */

/* Allocate a free slot, return slot index or -1 */
static int tile_alloc_slot(gui_ctx_t* ctx)
{
    if (ctx->tile_free_map == 0) return -1;
    uint8_t slot = (uint8_t)__builtin_ctz((unsigned int)ctx->tile_free_map);
    ctx->tile_free_map &= ~(1U << slot);
    tile_t* t = &ctx->tiles[slot];
    t->gen = (t->gen == 15) ? 1 : (t->gen + 1);
    t->flags = TILE_F_USED | TILE_F_VISIBLE;
    t->z = 0;
    memset(&t->win, 0, sizeof(t->win));
    return (int)slot;
}

/* Free a slot */
static void tile_free_slot(gui_ctx_t* ctx, uint8_t slot)
{
    ctx->tiles[slot].flags = 0;
    ctx->tiles[slot].win.objbuf = NULL;
    ctx->tiles[slot].win.objnum = 0;
    ctx->tile_free_map |= (1U << slot);
}

/* Validate handle and return slot index, or -1 */
static int tile_slot_by_handle(gui_ctx_t* ctx, tile_h_t h)
{
    if (h == 0) return -1;
    uint8_t slot = TILE_SLOT(h);
    if (slot >= TILE_MAX) return -1;
    tile_t* t = &ctx->tiles[slot];
    if (!(t->flags & TILE_F_USED)) return -1;
    if (TILE_MAKE_HANDLE(t->gen, slot) != h) return -1;
    return (int)slot;
}

/* Fallback: select any remaining tile when active is deleted */
static void tile_active_fallback(gui_ctx_t* ctx)
{
    for (uint8_t i = 0; i < TILE_MAX; i++) {
        if (ctx->tiles[i].flags & TILE_F_USED) {
            ctx->active_handle = TILE_MAKE_HANDLE(ctx->tiles[i].gen, i);
            ctx->active_slot = i;
            return;
        }
    }
    ctx->active_handle = 0;
    ctx->active_slot = 0xFF;
}

/* ================================================================
 * SPI device functions
 * ================================================================ */
static void gui_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    gui_ctx_t* ctx = (gui_ctx_t*)data;
    app_t* sspi = ctx->sspi;
    uint16_t ex = Gx + width - 1;
    uint16_t ey = Gy + height - 1;
    uint8_t ca[] = {Gx >> 8, Gx & 0xFF, ex >> 8, ex & 0xFF};
    uint8_t ra[] = {Gy >> 8, Gy & 0xFF, ey >> 8, ey & 0xFF};
    uint8_t cmd;
    cmd = 0x2A; appwrite(sspi, &cmd, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_CMD));
    appwrite(sspi, ca, 4, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT));
    cmd = 0x2B; appwrite(sspi, &cmd, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_CMD));
    appwrite(sspi, ra, 4, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT));
    cmd = 0x2C; appwrite(sspi, &cmd, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_CMD));
}

static void gui_pixels(void* data, const KSCCOLOR* colors, uint16_t num)
{
    gui_ctx_t* ctx = (gui_ctx_t*)data;
    app_t* sspi = ctx->sspi;
    uint8_t* pixbuf = ctx->pixbuf;
    uint16_t batch = sizeof(ctx->pixbuf) / 2;
    while (num) {
        uint16_t n = (num > batch) ? batch : num;
        uint8_t* p = pixbuf;
        for (uint16_t i = 0; i < n; i++) {
            KSCCOLOR c = colors[i];
            *p++ = c >> 8;
            *p++ = c & 0xFF;
        }
        appwrite(sspi, pixbuf, n * 2, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT_DMA));
        colors += n;
        num -= n;
    }
}

static void gui_window_setcanvas(k_draw_device* dev, KSC_window* screen,
    uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    gui_setcanvas(dev->data, Gx + screen->ssx, Gy + screen->ssy, width, height);
}

static void gui_window_setpixels(k_draw_device* dev, KSC_window* screen,
    const KSCCOLOR* color, uint16_t num)
{
    (void)screen;
    gui_pixels(dev->data, color, num);
}

/* ================================================================
 * ST7789 initialization
 * ================================================================ */
static void gui_init_st7789(void* data)
{
    gui_ctx_t* ctx = (gui_ctx_t*)data;
    app_t* sspi = ctx->sspi;
    appwrite(sspi, NULL, 0, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_PULSE_R1));
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
        appwrite(sspi, &cmd, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_CMD));
        if (n) appwrite(sspi, (void*)p, n, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT));
        p += n;
    }
}

/* ================================================================
 * App lifecycle
 * ================================================================ */
static int gui_open(app_t* app)
{
    if (app->app_data) return 0;  /* idempotent */
    gui_ctx_t* ctx = (gui_ctx_t*)osmalloc(sizeof(gui_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(gui_ctx_t));

    ctx->sspi = app->app0;      /* super_spi (unified) */
    ctx->tile_free_map = 0xFFFF; /* all slots free */

    ctx->dev.data = ctx;
    ctx->dev.init = gui_init_st7789;
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;
    ctx->dev.setpixels = gui_window_setpixels;
    kobjdraw_init(&ctx->dev);

    ctx->spi_dev[0] = -1;
    ctx->spi_dev[1] = -1;
    if (!ctx->sspi) { osfree(ctx); return -1; }
    if (appopen(ctx->sspi) < 0) { osfree(ctx); return -1; }

    ctx->spi_dev[0] = appioctl(ctx->sspi, "reg", 1);
    if (ctx->spi_dev[0] < 0) { osfree(ctx); return -1; }
    appioctl(ctx->sspi, "setpin", 1, ctx->spi_dev[0], SSPI_CS,  4);
    appioctl(ctx->sspi, "setpin", 1, ctx->spi_dev[0], SSPI_DC,  2);
    appioctl(ctx->sspi, "setpin", 1, ctx->spi_dev[0], SSPI_R1,  3);

    ctx->spi_dev[1] = appioctl(ctx->sspi, "reg", 2);
    if (ctx->spi_dev[1] < 0) { osfree(ctx); return -1; }
    appioctl(ctx->sspi, "setpin", 2, ctx->spi_dev[1], SSPI_CS, 12);
    appioctl(ctx->sspi, "setpin", 2, ctx->spi_dev[1], SSPI_DC,  8);
    appioctl(ctx->sspi, "setpin", 2, ctx->spi_dev[1], SSPI_R1,  9);

    /* Default SPI: SPI2 first */
    ctx->sspi_inst = 2;
    ctx->sspi_dev  = ctx->spi_dev[1];

    /* Default tile 0: full screen */
    {
        int slot = tile_alloc_slot(ctx);
        tile_t* t = &ctx->tiles[slot];
        t->win.ssx = 0;
        t->win.ssy = 0;
        t->win.width = TFT_W;
        t->win.height = TFT_H;
        t->win.bk = 0;
        t->z = 0;
        ctx->active_handle = TILE_MAKE_HANDLE(t->gen, (uint8_t)slot);
        ctx->active_slot = (uint8_t)slot;
    }

    app->app_data = ctx;
    return 0;
}

static int gui_close(app_t* app)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    if (!ctx) return 0;  /* idempotent */
    if (ctx->sspi) appclose(ctx->sspi);
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

/* ================================================================
 * Command handlers
 * ================================================================ */

/* --- lifecycle (no tile needed) --- */
static int handler_setspi(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    int n = va_arg(ap, int);
    if (n < 1 || n > 2) return 0;
    if (n == ctx->sspi_inst) return 0;
    ctx->sspi_inst = n;
    ctx->sspi_dev  = ctx->spi_dev[n - 1];
    return 1;
}

static int handler_init(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr; (void)ap;
    if (!ctx->sspi) return 0;
    ctx->dev.init(ctx);
    return 1;
}

static int handler_orient(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    uint8_t orient = (uint8_t)va_arg(ap, int);
    uint8_t mcmd = 0x36;
    appwrite(ctx->sspi, &mcmd, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_CMD));
    appwrite(ctx->sspi, &orient, 1, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT));
    return 1;
}

/* --- tile lifecycle (no tile needed) --- */
static int handler_wcreate(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint16_t w = (uint16_t)va_arg(ap, int);
    uint16_t h = (uint16_t)va_arg(ap, int);
    KSCCOLOR bk = (KSCCOLOR)va_arg(ap, int);

    int slot = tile_alloc_slot(ctx);
    if (slot < 0) return 0;

    tile_t* t = &ctx->tiles[slot];
    t->win.ssx = x;
    t->win.ssy = y;
    t->win.width = w;
    t->win.height = h;
    t->win.bk = bk;

    /* Auto Z: one above current max */
    uint8_t max_z = 0;
    for (uint8_t i = 0; i < TILE_MAX; i++) {
        if (ctx->tiles[i].flags & TILE_F_USED)
            if (ctx->tiles[i].z > max_z) max_z = ctx->tiles[i].z;
    }
    t->z = max_z + 1;

    tile_h_t hnd = TILE_MAKE_HANDLE(t->gen, (uint8_t)slot);

    /* Auto-clear: 填充背景色 */
    kfull(&ctx->dev, &t->win, bk, x, y, w, h);

    return (int)(uint8_t)hnd;
}

static int handler_wdelete(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    uint8_t was_active = (ctx->active_handle == h) ? 1 : 0;
    tile_free_slot(ctx, (uint8_t)slot);
    if (was_active) tile_active_fallback(ctx);
    return 1;
}

static int handler_wselect(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->active_handle = h;
    ctx->active_slot = (uint8_t)slot;
    return 1;
}

/* --- tile visibility (no tile needed) --- */
static int handler_whide(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags &= ~TILE_F_VISIBLE;
    return 1;
}

static int handler_wshew(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags |= TILE_F_VISIBLE;
    return 1;
}

static int handler_wtoggle(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags ^= TILE_F_VISIBLE;
    return 1;
}

/* --- tile properties (no tile needed) --- */
static int handler_wmove(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.ssx = (uint16_t)va_arg(ap, int);
    ctx->tiles[slot].win.ssy = (uint16_t)va_arg(ap, int);
    return 1;
}

static int handler_wresize(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.width = (uint16_t)va_arg(ap, int);
    ctx->tiles[slot].win.height = (uint16_t)va_arg(ap, int);
    return 1;
}

static int handler_wbk(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.bk = (KSCCOLOR)va_arg(ap, int);
    return 1;
}

static int handler_wzorder(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].z = (uint8_t)va_arg(ap, int);
    return 1;
}

/* --- tile query (no tile needed) --- */
static int handler_wactive(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr; (void)ap;
    return (int)(uint8_t)ctx->active_handle;
}

static int handler_winfo(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    tile_info_t* info = va_arg(ap, tile_info_t*);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0 || !info) return 0;
    tile_t* t = &ctx->tiles[slot];
    info->handle = h;
    info->x = t->win.ssx;
    info->y = t->win.ssy;
    info->w = t->win.width;
    info->h = t->win.height;
    info->bk = t->win.bk;
    info->visible = (t->flags & TILE_F_VISIBLE) ? 1 : 0;
    info->z = t->z;
    info->is_active = (ctx->active_handle == h) ? 1 : 0;
    info->obj_count = t->win.objnum;
    return 1;
}

static int handler_wenum(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t* buf = va_arg(ap, tile_h_t*);
    int* count_ptr = va_arg(ap, int*);
    if (!buf || !count_ptr) return 0;
    int max_out = *count_ptr;
    int written = 0;
    for (uint8_t i = 0; i < TILE_MAX && written < max_out; i++) {
        if (ctx->tiles[i].flags & TILE_F_USED) {
            buf[written++] = TILE_MAKE_HANDLE(ctx->tiles[i].gen, i);
        }
    }
    *count_ptr = written;
    return written;
}

/* --- explicit render (no tile needed) --- */

/* Collect visible tile indices sorted by Z */
static uint8_t tile_collect_sorted(gui_ctx_t* ctx, uint8_t* out, uint8_t max_out)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < TILE_MAX && count < max_out; i++) {
        if ((ctx->tiles[i].flags & (TILE_F_USED|TILE_F_VISIBLE)) == (TILE_F_USED|TILE_F_VISIBLE))
            out[count++] = i;
    }
    /* Insertion sort by Z ascending */
    for (uint8_t i = 1; i < count; i++) {
        uint8_t key = out[i];
        int j = (int)i - 1;
        while (j >= 0 && ctx->tiles[out[j]].z > ctx->tiles[key].z) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

static int handler_trenderall(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr; (void)ap;
    uint8_t order[TILE_MAX];
    uint8_t count = tile_collect_sorted(ctx, order, TILE_MAX);
    for (uint8_t i = 0; i < count; i++) {
        tile_t* t = &ctx->tiles[order[i]];
        kobjsdraw(&ctx->dev, &t->win, t->win.objbuf, t->win.objnum);
    }
    return 1;
}

static int handler_tredraw(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    if (!(t->flags & TILE_F_VISIBLE)) return 0;
    kobjsdraw(&ctx->dev, &t->win, t->win.objbuf, t->win.objnum);
    return 1;
}

static int handler_trender(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    if (!(t->flags & TILE_F_VISIBLE)) return 0;
    kobjsdraw(&ctx->dev, &t->win, t->win.objbuf, t->win.objnum);
    return 1;
}

/* --- drawing: active tile clear --- */
static int handler_wclear(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)ap;
    kfull(&ctx->dev, scr, scr->bk, 0, 0, scr->width, scr->height);
    return 1;
}

static int handler_clear(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kfull(&ctx->dev, scr, c, 0, 0, scr->width, scr->height);
    return 1;
}

/* --- drawing: primitives on active tile --- */
static int handler_pixel(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    ksetpixel(&ctx->dev, scr, c, x, y);
    return 1;
}

static int handler_fill(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint16_t w = (uint16_t)va_arg(ap, int);
    uint16_t h = (uint16_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kfull(&ctx->dev, scr, c, x, y, w, h);
    return 1;
}

static int handler_rect(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint16_t w = (uint16_t)va_arg(ap, int);
    uint16_t h = (uint16_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kbox(&ctx->dev, scr, c, x, y, w, h);
    return 1;
}

#if __DRAW_CIRCLE__
static int handler_circle(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint8_t  r = (uint8_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kcircle(&ctx->dev, scr, c, x, y, r);
    return 1;
}

static int handler_fcircle(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint8_t  r = (uint8_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kfillcircle(&ctx->dev, scr, c, x, y, r);
    return 1;
}
#endif

static int handler_line(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x0 = (uint16_t)va_arg(ap, int);
    uint16_t y0 = (uint16_t)va_arg(ap, int);
    uint16_t x1 = (uint16_t)va_arg(ap, int);
    uint16_t y1 = (uint16_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kline(&ctx->dev, scr, c, x0, y0, x1, y1);
    return 1;
}

#if __DRAW_CIRCLE__
static int handler_arc(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint8_t  r = (uint8_t)va_arg(ap, int);
    uint8_t  d = (uint8_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    karc(&ctx->dev, scr, c, x, y, r, d);
    return 1;
}

static int handler_rrect(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint16_t w = (uint16_t)va_arg(ap, int);
    uint16_t h = (uint16_t)va_arg(ap, int);
    uint8_t  r = (uint8_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kroundrect(&ctx->dev, scr, c, x, y, w, h, r);
    return 1;
}

static int handler_frrect(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x = (uint16_t)va_arg(ap, int);
    uint16_t y = (uint16_t)va_arg(ap, int);
    uint16_t w = (uint16_t)va_arg(ap, int);
    uint16_t h = (uint16_t)va_arg(ap, int);
    uint8_t  r = (uint8_t)va_arg(ap, int);
    KSCCOLOR c = (KSCCOLOR)va_arg(ap, int);
    kfillroundrect(&ctx->dev, scr, c, x, y, w, h, r);
    return 1;
}
#endif

static int handler_char(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x  = (uint16_t)va_arg(ap, int);
    uint16_t y  = (uint16_t)va_arg(ap, int);
    char     ch = (char)va_arg(ap, int);
    KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
    KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
    kchar(&ctx->dev, scr, ch, x, y, fg, bg);
    return 1;
}

static int handler_string(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x  = (uint16_t)va_arg(ap, int);
    uint16_t y  = (uint16_t)va_arg(ap, int);
    const char* s = va_arg(ap, const char*);
    KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
    KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
    kstring(&ctx->dev, scr, s, x, y, fg, bg);
    return 1;
}

#if __USE_CHINESE__
static int handler_strcn(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    uint16_t x  = (uint16_t)va_arg(ap, int);
    uint16_t y  = (uint16_t)va_arg(ap, int);
    const char* s = va_arg(ap, const char*);
    KSCCOLOR fg = (KSCCOLOR)va_arg(ap, int);
    KSCCOLOR bg = (KSCCOLOR)va_arg(ap, int);
    kstringchinese(&ctx->dev, scr, s, x, y, fg, bg);
    return 1;
}
#endif

/* --- image (direct SPI fast-path, on active tile) --- */
static int handler_image(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
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
        appwrite(ctx->sspi, ctx->pixbuf, n, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT_DMA));
        img += n;
        remain -= n;
    }
    return 1;
}

static int handler_ibig(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
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

static int handler_ibin(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
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

/* ================================================================
 * appwrite dispatch (高频: 不走 ioctl strcmp)
 * mode 低4位: 1=DRAWOBJ, 2=DRAWOBJS
 * ================================================================ */
static int gui_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    if (!ctx || !ctx->active_handle) return 0;
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;

    switch (mode & 0x0F) {
    case 0x01:  /* DRAWOBJ */
        if (!data || count != 1) return 0;
        kobjdraw(&ctx->dev, scr, (ksc_obj_t*)data);
        return 1;
    case 0x02:  /* DRAWOBJS */
        if (!data || count == 0) return 0;
        kobjsdraw(&ctx->dev, scr, (ksc_obj_t*)data, (uint16_t)count);
        return (int)count;
    }
    return 0;
}

/* --- ioctl object pool / drawfunc (低频) --- */
static int handler_getobjpool(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    int* count = va_arg(ap, int*);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0 || !count) return 0;
    tile_t* t = &ctx->tiles[slot];
    *count = (int)t->win.objnum;
    return (int)(intptr_t)t->win.objbuf;
}

static int handler_setobjpool(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)ctx; (void)scr;
    tile_h_t h = (tile_h_t)va_arg(ap, int);
    ksc_obj_t* objs = va_arg(ap, ksc_obj_t*);
    int count = va_arg(ap, int);
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    t->win.objbuf = objs;
    t->win.objnum = (uint8_t)count;
    return 1;
}

static int handler_setdrawfunc(gui_ctx_t* ctx, KSC_window* scr, va_list ap)
{
    (void)ctx; (void)scr;
    int idx = va_arg(ap, int);
    draw_fn fn = va_arg(ap, draw_fn);
    return (ksc_set_draw_func((uint8_t)idx, fn) == 0) ? 1 : 0;
}

/* ================================================================
 * Command dispatch table
 * ================================================================ */
static const cmd_entry_t cmd_table[] = {
    /* lifecycle */
    {"setspi",  handler_setspi,  0},
    {"init",    handler_init,    0},
    {"orient",  handler_orient,  0},
    /* tile lifecycle */
    {"wcreate", handler_wcreate, 0},
    {"wdelete", handler_wdelete, 0},
    {"wselect", handler_wselect, 0},
    /* tile visibility */
    {"whide",   handler_whide,   0},
    {"wshew",   handler_wshew,   0},
    {"wtoggle", handler_wtoggle, 0},
    /* tile properties */
    {"wmove",   handler_wmove,   0},
    {"wresize", handler_wresize, 0},
    {"wbk",     handler_wbk,     0},
    {"wzorder", handler_wzorder, 0},
    /* tile query */
    {"wactive", handler_wactive, 0},
    {"winfo",   handler_winfo,   0},
    {"wenum",   handler_wenum,   0},
    /* explicit render */
    {"trenderall", handler_trenderall, 0},
    {"tredraw", handler_tredraw, 0},
    {"trender", handler_trender, 0},
    /* active tile drawing */
    {"wclear",  handler_wclear,  CMD_NEEDS_TILE},
    {"clear",   handler_clear,   CMD_NEEDS_TILE},
    {"pixel",   handler_pixel,   CMD_NEEDS_TILE},
    {"fill",    handler_fill,    CMD_NEEDS_TILE},
    {"frect",   handler_fill,    CMD_NEEDS_TILE},
    {"rect",    handler_rect,    CMD_NEEDS_TILE},
#if __DRAW_CIRCLE__
    {"circle",  handler_circle,  CMD_NEEDS_TILE},
    {"fcircle", handler_fcircle, CMD_NEEDS_TILE},
#endif
    {"line",    handler_line,    CMD_NEEDS_TILE},
#if __DRAW_CIRCLE__
    {"arc",     handler_arc,     CMD_NEEDS_TILE},
    {"rrect",   handler_rrect,   CMD_NEEDS_TILE},
    {"frrect",  handler_frrect,  CMD_NEEDS_TILE},
#endif
    {"char",    handler_char,    CMD_NEEDS_TILE},
    {"string",  handler_string,  CMD_NEEDS_TILE},
#if __USE_CHINESE__
    {"strcn",   handler_strcn,   CMD_NEEDS_TILE},
#endif
    {"image",   handler_image,   CMD_NEEDS_TILE},
    {"ibig",    handler_ibig,    CMD_NEEDS_TILE},
    {"ibin",    handler_ibin,    CMD_NEEDS_TILE},
    /* object pool */
    {"getobjpool", handler_getobjpool, 0},
    {"setobjpool", handler_setobjpool, 0},
    {"setdrawfunc", handler_setdrawfunc, 0},
};
#define CMD_TABLE_SIZE (sizeof(cmd_table)/sizeof(cmd_table[0]))

/* ================================================================
 * ioctl dispatcher
 * ================================================================ */
static int gui_ioctl(app_t* app, const char* cmd, va_list ap)
{
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(cmd, cmd_table[i].name) == 0) {
            if (cmd_table[i].flags & CMD_NEEDS_TILE) {
                if (!ctx->active_handle) return 0;
                KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
                return cmd_table[i].handler(ctx, scr, ap);
            }
            return cmd_table[i].handler(ctx, NULL, ap);
        }
    }
    return 0;
}

/* ================================================================
 * App descriptor
 * ================================================================ */
static const papp_ops_t kscgui_ops = {
    .open  = gui_open,
    .close = gui_close,
    .write = gui_write,
    .ioctl = gui_ioctl,
};

REGISTER_APP_EX("KSCGUI", "0", "1\0super_spi", &kscgui_ops,
    "KSC GUI Manager (Tile-based, 16 slots, Z-order compositing)");

#endif
