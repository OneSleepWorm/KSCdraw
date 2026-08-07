/**
 * @file    kscgui.c
 * @note    GUI 管理器 — Tile 合成器 + ST7789 驱动 (STM32) / EasyX (PC)
 * @flash   ~6724B (Debug, -Og) / 文本+只读数据
 *
 * ============================================================
 * 使用说明 — appcmd 接口（推荐方式）
 * ============================================================
 *
 * 所有绘图通过 k_draw_device 完成.
 *
 * --- 快速开始 ---
 *   app_t* gui = appget("KSCGUI");
 *   appopen(gui);
 *   appcmd(gui, "init");                    // 初始化 + 创建全屏 tile
 *   appcmd(gui, "fill -x 0 -y 0 -w 240 -h 320 -c F800");  // 全屏红色
 *
 * --- 图元命令（参数标志风格）---
 *
 *   填充 / 清除
 *     fill -x <x> -y <y> -w <w> -h <h> -c <color>
 *     clear -c <color>         清除整个 active tile
 *     wclear                   清除 active tile 为其背景色 (tile->bk)
 *
 *   矩形
 *     rect -x <x> -y <y> -w <w> -h <h> -c <color>
 *     fill -x <x> -y <y> -w <w> -h <h> -c <color>
 *
 *   圆形 / 圆弧 (需 __DRAW_CIRCLE__=1)
 *     circle -x <cx> -y <cy> -r <r> -c <color>
 *     fcircle -x <cx> -y <cy> -r <r> -c <color>
 *     arc -x <cx> -y <cy> -r <r> -d <dir> -c <color>
 *
 *   圆角矩形
 *     rrect -x <x> -y <y> -w <w> -h <h> -r <radius> -c <color>
 *     frrect -x <x> -y <y> -w <w> -h <h> -r <radius> -c <color>
 *
 *   线段
 *     line -x <x1> -y <y1> -w <x2> -z <y2> -c <color>
 *
 *   像素
 *     pixel -x <x> -y <y> -c <color>
 *
 *   字符 / 字符串
 *     char -x <x> -y <y> -v <ascii> -c <fg> -b <bg>
 *     string -x <x> -y <y> -s <text> -c <fg> -b <bg>
 *
 *   图像 (直接从内存绘制)
 *     image -x <x> -y <y> -w <w> -h <h>  (data = app->input_data)
 *     ibin  -x <x> -y <y> -w <w> -h <h> -c <fg> -b <bg>
 *     ibig  -x <x> -y <y> -w <w> -h <h> -s <scale>
 *
 * --- Tile 管理 ---
 *     wcreate -x <x> -y <y> -w <w> -h <h> -c <bk>
 *     wdelete -t <handle>
 *     wselect -t <handle>
 *     whide   -t <handle>
 *     wshew   -t <handle>
 *     wtoggle -t <handle>
 *     wmove   -t <handle> -x <x> -y <y>
 *     wresize -t <handle> -w <w> -h <h>
 *     wbk     -t <handle> -c <bk>
 *     wzorder -t <handle> -z <z>
 *     wactive                    -> 返回 active 句柄
 *     winfo   -t <handle>        通过 app->output_data 返回 tile_info_t
 *     wenum                      通过 app->input_data + output_data 枚举
 *
 * --- 渲染 ---
 *     trenderall                 遍历所有 visible tile 按 Z 序重绘
 *     tredraw  -t <handle>      显式重绘指定 tile
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
 *     mode 0x01 -> DRAWOBJ  (单 ksc_obj_t)
 *     mode 0x02 -> DRAWOBJS (ksc_obj_t 数组)
 *
 *   低频（配置/管理）：appcmd，经 strcmp 查表
 *     tile 生命周期: wcreate/wdelete/wselect/whide/...
 *     绘图命令: clear/fill/pixel/line/rect/string/...
 *     obj 池: setobjpool/getobjpool
 *     绘制函数槽: setdrawfunc
 *
 * ============================================================
 * 注册名:    KSCGUI
 * 平台:      STM32 (依赖 super_spi) / PC (自包含 EasyX)
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/KSCdraw.h"
#include "../inc/KSCOSsystem.h"
#include "app_config.h"
#include <string.h>

/* Cast helper for C++ (PC builds compile as CXX) */
#define GUI_CTX(app) ((gui_ctx_t*)(app)->app_data)

/* 绘图命令前置检查: 屏幕必须已 cmd_init (hw_inited), 且存在 active tile。
 * 未 init 直接拒绝 — 与 STM32 一致, 且避免 PC easyx 未建窗崩溃。 */
#define GUI_REQUIRE_HW(ctx) \
    do { if (!(ctx) || !(ctx)->hw_inited || !(ctx)->active_handle) return -1; } while (0)

/* ================================================================
 * Constants (SHARED)
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

/* ================================================================
 * Types (SHARED)
 * ================================================================ */
typedef struct {
    KSC_window  win;         /* ssx/ssy/width/height/bk/objbuf/objnum/Mode */
    uint8_t     gen;         /* generation 1..15, 0=slot free */
    uint8_t     flags;       /* TILE_F_USED | TILE_F_VISIBLE */
    uint8_t     z;           /* Z-order, higher = drawn later = on top */
} tile_t;

typedef struct {
#if __USE_STM32__
    app_t*          sspi;           /* super_spi (unified) */
    app_t*          gpio;           /* gpio_port (CS/DC/RST) */
    int             sspi_inst;      /* active SPI instance: 1 or 2 */
    int             sspi_dev;       /* device ID on active instance */
    int             spi_dev[2];     /* [0]=SPI1 dev, [1]=SPI2 dev */
#endif
    k_draw_device   dev;
    tile_t          tiles[TILE_MAX];
    uint16_t        tile_free_map;  /* bitmap: 1=slot free */
    tile_h_t        active_handle;  /* 0=none */
    uint8_t         active_slot;    /* cache of active tile's slot index */
    uint8_t         hw_inited;      /* 1=cmd_init 已完成屏幕初始化 */
#if __USE_STM32__
    uint8_t         pixbuf[512];
#endif
} gui_ctx_t;

/* ================================================================
 * Internal helpers (SHARED)
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

static uint8_t tile_collect_sorted(gui_ctx_t* ctx, uint8_t* order, uint8_t max)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < TILE_MAX && n < max; i++) {
        if (ctx->tiles[i].flags & TILE_F_USED)
            order[n++] = i;
    }
    for (uint8_t i = 0; i < n; i++) {
        for (uint8_t j = i + 1; j < n; j++) {
            if (ctx->tiles[order[j]].z < ctx->tiles[order[i]].z) {
                uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
            }
        }
    }
    return n;
}

/* ================================================================
 * STM32: SPI device functions
 * ================================================================ */
#if __USE_STM32__
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
#endif /* __USE_STM32__ */

/* ================================================================
 * App lifecycle (PLATFORM SPECIFIC)
 * ================================================================ */
#if __USE_STM32__
static int gui_open(app_t* app)
{
    if (app->app_data) return 0;
    gui_ctx_t* ctx = (gui_ctx_t*)osmalloc(sizeof(gui_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(gui_ctx_t));

    ctx->sspi = app->app0;
    ctx->gpio = appget("gpio_port");
    if (ctx->gpio) appopen(ctx->gpio);
    ctx->tile_free_map = 0xFFFF;

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

    ctx->spi_dev[0] = appcmd(ctx->sspi, "reg -i 1");
    if (ctx->spi_dev[0] < 0) { osfree(ctx); return -1; }
    sspi_setpin(ctx->sspi, 1, ctx->spi_dev[0], SSPI_CS,  4);
    sspi_setpin(ctx->sspi, 1, ctx->spi_dev[0], SSPI_DC,  2);
    sspi_setpin(ctx->sspi, 1, ctx->spi_dev[0], SSPI_R1,  3);

    ctx->spi_dev[1] = appcmd(ctx->sspi, "reg -i 2");
    if (ctx->spi_dev[1] < 0) { osfree(ctx); return -1; }
    sspi_setpin(ctx->sspi, 2, ctx->spi_dev[1], SSPI_CS, 12);
    sspi_setpin(ctx->sspi, 2, ctx->spi_dev[1], SSPI_DC,  8);
    sspi_setpin(ctx->sspi, 2, ctx->spi_dev[1], SSPI_R1,  9);

    ctx->sspi_inst = 2;
    ctx->sspi_dev  = ctx->spi_dev[1];

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
    if (!ctx) return 0;
    if (ctx->sspi) appclose(ctx->sspi);
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}
#elif __USE_PC__
static int gui_open(app_t* app)
{
    if (app->app_data) return 0;
    gui_ctx_t* ctx = (gui_ctx_t*)osmalloc(sizeof(gui_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(gui_ctx_t));

    ctx->tile_free_map = 0xFFFF;

    k_draw_device* sys_dev = k_draw_device_init();
    if (!sys_dev) { osfree(ctx); return -1; }
    memcpy(&ctx->dev, sys_dev, sizeof(k_draw_device));
    kobjdraw_init(&ctx->dev);

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
    if (!ctx) return 0;
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}
#endif

/* ================================================================
 * appcmd: init (PLATFORM SPECIFIC)
 * ================================================================ */
#if __USE_STM32__
static int cmd_init(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (ctx->hw_inited) return 1;   /* 幂等 */
    app_t* sspi = ctx->sspi;
    app_t* gpio = ctx->gpio;
    if (!sspi || !gpio) return -1;

    appcmd(gpio, "cfg -p 28 -m 3");
    appcmd(gpio, "cfg -p 24 -m 3");
    appcmd(gpio, "cfg -p 25 -m 3");
    appcmd(gpio, "set -p 28 -v 1");
    appcmd(gpio, "set -p 24 -v 1");
    appcmd(gpio, "set -p 25 -v 1");

    appcmd(sspi, "init -i 2");

    appcmd(gpio, "set -p 25 -v 0");
    sysdelay(100);
    appcmd(gpio, "set -p 25 -v 1");
    sysdelay(150);

    static const uint8_t init_seq[] = {
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

    const uint8_t* p = init_seq;
    while (*p != 0xFF) {
        uint8_t cmd = *p++;
        uint8_t n = *p++;
        if (cmd == 0x00) { sysdelay(120); continue; }

        appcmd(gpio, "set -p 24 -v 0");
        appcmd(gpio, "set -p 28 -v 0");
        sspi->input_data = (void*)&cmd;
        sspi->mode_data = NULL;
        appcmd(sspi, "tx -i 2 -n 1");
        appcmd(gpio, "set -p 28 -v 1");
        appcmd(gpio, "set -p 24 -v 1");

        if (n) {
            sspi_mode_t md = { .len = n };
            sspi->mode_data = &md;
            sspi->input_data = (void*)p;
            appcmd(gpio, "set -p 28 -v 0");
            appcmd(sspi, "tx -i 2 -m");
            appcmd(gpio, "set -p 28 -v 1");
            sspi->mode_data = NULL;
        }
        p += n;
    }

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

    ctx->hw_inited = 1;
    return 1;
}
#elif __USE_PC__
static int cmd_init(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = (gui_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (!ctx->hw_inited) {
        screen_hw_init();       /* 创建 easyx 窗口 — 与 STM32 init 语义对齐 */
        ctx->hw_inited = 1;
    }
    return 1;
}
#endif

/* ================================================================
 * appcmd: drawing primitives (SHARED)
 * ================================================================ */

static int cmd_pixel(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') || !APPCMD_HAS(argv, 'c')) return -1;
    ksetpixel(&ctx->dev, scr, (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0));
    return 1;
}

static int cmd_fill(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') || !APPCMD_HAS(argv, 'c')) return -1;
    uint16_t x = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    uint16_t y = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    uint16_t w = (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    uint16_t h = (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    KSCCOLOR c = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    kfull(&ctx->dev, scr, c, x, y, w, h);
    return 1;
}

static int cmd_rect(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') || !APPCMD_HAS(argv, 'c')) return -1;
    kbox(&ctx->dev, scr, (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0));
    return 1;
}

static int cmd_line(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'z') || !APPCMD_HAS(argv, 'c')) return -1;
    kline(&ctx->dev, scr, (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('z')], NULL, 0));
    return 1;
}

static int cmd_char(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'v') || !APPCMD_HAS(argv, 'c') || !APPCMD_HAS(argv, 'b')) return -1;
    kchar(&ctx->dev, scr, (char)strtoul(argv[APPCMD_ARG('v')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('b')], NULL, 16));
    return 1;
}

static int cmd_string(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 's') || !APPCMD_HAS(argv, 'c') || !APPCMD_HAS(argv, 'b')) return -1;
    kstring(&ctx->dev, scr, argv[APPCMD_ARG('s')],
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('b')], NULL, 16));
    return 1;
}

#if __DRAW_CIRCLE__
static int cmd_circle(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'r') || !APPCMD_HAS(argv, 'c')) return -1;
    kcircle(&ctx->dev, scr,
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0));
    return 1;
}

static int cmd_fcircle(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'r') || !APPCMD_HAS(argv, 'c')) return -1;
    kfillcircle(&ctx->dev, scr,
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0));
    return 1;
}

static int cmd_arc(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'r') || !APPCMD_HAS(argv, 'd') || !APPCMD_HAS(argv, 'c')) return -1;
    karc(&ctx->dev, scr,
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('d')], NULL, 0));
    return 1;
}

static int cmd_rrect(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') ||
        !APPCMD_HAS(argv, 'r') || !APPCMD_HAS(argv, 'c')) return -1;
    kroundrect(&ctx->dev, scr,
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0));
    return 1;
}

static int cmd_frrect(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') ||
        !APPCMD_HAS(argv, 'r') || !APPCMD_HAS(argv, 'c')) return -1;
    kfillroundrect(&ctx->dev, scr,
        (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16),
        (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0),
        (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0),
        (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0));
    return 1;
}
#endif

/* ================================================================
 * appcmd: tile operations (SHARED)
 * ================================================================ */

static tile_h_t resolve_tile_handle(app_t* app, gui_ctx_t* ctx, const char** argv)
{
    if (APPCMD_HAS(argv, 't'))
        return (tile_h_t)strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (app->mode_data)
        return (tile_h_t)(uintptr_t)app->mode_data;
    return ctx->active_handle;
}

static int cmd_wclear(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    int slot;
    if (h) {
        slot = tile_slot_by_handle(ctx, h);
    } else if (ctx->active_handle) {
        slot = ctx->active_slot;
    } else {
        return -1;
    }
    if (slot < 0) return 0;
    KSC_window* scr = &ctx->tiles[slot].win;
    kfull(&ctx->dev, scr, scr->bk, 0, 0, scr->width, scr->height);
    return 1;
}

static int cmd_clear(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'c')) return -1;
    KSCCOLOR c = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    kfull(&ctx->dev, scr, c, 0, 0, scr->width, scr->height);
    return 1;
}

static int cmd_wcreate(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') || !APPCMD_HAS(argv, 'c')) return -1;
    int slot = tile_alloc_slot(ctx);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    t->win.ssx = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    t->win.ssy = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    t->win.width = (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    t->win.height = (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    t->win.bk = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    uint8_t max_z = 0;
    for (uint8_t i = 0; i < TILE_MAX; i++)
        if ((ctx->tiles[i].flags & TILE_F_USED) && ctx->tiles[i].z > max_z)
            max_z = ctx->tiles[i].z;
    t->z = max_z + 1;
    tile_h_t hnd = TILE_MAKE_HANDLE(t->gen, (uint8_t)slot);
    kfull(&ctx->dev, &t->win, t->win.bk, 0, 0, t->win.width, t->win.height);
    app->output_data = (void*)(uintptr_t)hnd;
    return (int)(uint8_t)hnd;
}

static int cmd_wdelete(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    uint8_t was_active = (ctx->active_handle == h) ? 1 : 0;
    tile_free_slot(ctx, (uint8_t)slot);
    if (was_active) tile_active_fallback(ctx);
    return 1;
}

static int cmd_wselect(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->active_handle = h;
    ctx->active_slot = (uint8_t)slot;
    return 1;
}

static int cmd_whide(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags &= ~TILE_F_VISIBLE;
    return 1;
}

static int cmd_wshew(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags |= TILE_F_VISIBLE;
    return 1;
}

static int cmd_wtoggle(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].flags ^= TILE_F_VISIBLE;
    return 1;
}

static int cmd_wmove(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y')) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.ssx = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    ctx->tiles[slot].win.ssy = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    return 1;
}

static int cmd_wresize(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h')) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.width = (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    ctx->tiles[slot].win.height = (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    return 1;
}

static int cmd_wbk(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !APPCMD_HAS(argv, 'c')) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.bk = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    return 1;
}

static int cmd_wzorder(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !APPCMD_HAS(argv, 'z')) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].z = (uint8_t)strtoul(argv[APPCMD_ARG('z')], NULL, 0);
    return 1;
}

static int cmd_wactive(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    return (int)(uint8_t)ctx->active_handle;
}

static int cmd_winfo(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    tile_info_t* info = (tile_info_t*)app->input_data;
    if (!info) return 0;
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

static int cmd_wenum(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t* buf = (tile_h_t*)app->input_data;
    int* count_ptr = (int*)app->mode_data;
    if (!buf || !count_ptr) return 0;
    int max_out = *count_ptr;
    int written = 0;
    for (uint8_t i = 0; i < TILE_MAX && written < max_out; i++) {
        if (ctx->tiles[i].flags & TILE_F_USED)
            buf[written++] = TILE_MAKE_HANDLE(ctx->tiles[i].gen, i);
    }
    *count_ptr = written;
    return written;
}

/* ================================================================
 * appcmd: render (SHARED)
 * ================================================================ */

static int cmd_trenderall(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    uint8_t order[TILE_MAX];
    uint8_t count = tile_collect_sorted(ctx, order, TILE_MAX);
    for (uint8_t i = 0; i < count; i++) {
        tile_t* t = &ctx->tiles[order[i]];
        kobjsdraw(&ctx->dev, &t->win, t->win.objbuf, t->win.objnum);
    }
    return 1;
}

static int cmd_tredraw(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    tile_t* t = &ctx->tiles[slot];
    if (!(t->flags & TILE_F_VISIBLE)) return 0;
    kobjsdraw(&ctx->dev, &t->win, t->win.objbuf, t->win.objnum);
    return 1;
}

static int cmd_trender(app_t* app, const char** argv)
{
    return cmd_tredraw(app, argv);
}

/* ================================================================
 * appcmd: object pool / drawfunc (SHARED)
 * ================================================================ */

static int cmd_setobjpool(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !APPCMD_HAS(argv, 'n')) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    ctx->tiles[slot].win.objbuf = (ksc_obj_t*)app->input_data;
    ctx->tiles[slot].win.objnum = (uint8_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    return 1;
}

static int cmd_getobjpool(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx) return -1;
    tile_h_t h = resolve_tile_handle(app, ctx, argv);
    if (!h) return -1;
    int slot = tile_slot_by_handle(ctx, h);
    if (slot < 0) return 0;
    if (app->input_data) *(int*)app->input_data = (int)ctx->tiles[slot].win.objnum;
    return (int)(intptr_t)ctx->tiles[slot].win.objbuf;
}

static int cmd_setdrawfunc(app_t* app, const char** argv)
{
    if (!APPCMD_HAS(argv, 'i')) return -1;
    draw_fn fn = (draw_fn)app->input_data;
    return (ksc_set_draw_func((uint8_t)strtoul(argv[APPCMD_ARG('i')], NULL, 0), fn) == 0) ? 1 : 0;
}

/* ================================================================
 * appcmd: image drawing (PLATFORM — STM32 uses direct SPI; PC uses KSCdraw)
 * ================================================================ */
#if __USE_STM32__
static int cmd_drawrow(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') || !APPCMD_HAS(argv, 'w')) return -1;
    uint16_t x = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    uint16_t y = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    uint16_t w = (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    const uint8_t* img = (const uint8_t*)app->input_data;
    if (!img) return -1;
    uint32_t n = (uint32_t)w * 2;
    if (n > sizeof(ctx->pixbuf)) return -1;
    gui_window_setcanvas(&ctx->dev, scr, x, y, w, 1);
    memcpy(ctx->pixbuf, img, n);
    appwrite(ctx->sspi, ctx->pixbuf, n, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT_DMA));
    return 1;
}

static int cmd_image(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h')) return -1;
    uint16_t x = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    uint16_t y = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    uint16_t w = (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    uint16_t h = (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    const uint8_t* img = (const uint8_t*)app->input_data;
    if (!img) return -1;
    gui_window_setcanvas(&ctx->dev, scr, x, y, w, h);
    uint32_t remain = (uint32_t)w * h * 2;
    while (remain) {
        uint16_t n = (remain > sizeof(ctx->pixbuf)) ? (uint16_t)sizeof(ctx->pixbuf) : (uint16_t)remain;
        memcpy(ctx->pixbuf, img, n);
        appwrite(ctx->sspi, ctx->pixbuf, n, SSPI_MODE(ctx->sspi_inst, ctx->sspi_dev, SSPI_SEND_DAT_DMA));
        img += n;
        remain -= n;
    }
    return 1;
}
#endif /* __USE_STM32__ */

/* ================================================================
 * appcmd: image drawing primitives (SHARED)
 * ================================================================ */

static int cmd_ibig(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') || !APPCMD_HAS(argv, 's')) return -1;
    uint16_t x = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    uint16_t y = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    uint8_t w = (uint8_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    uint8_t h = (uint8_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    uint8_t s = (uint8_t)strtoul(argv[APPCMD_ARG('s')], NULL, 0);
    const uint8_t* img = (const uint8_t*)app->input_data;
    if (!img) return -1;
    for (uint8_t hh = 0; hh < h; hh++) {
        for (uint8_t ww = 0; ww < w; ww++) {
            KSCCOLOR c = ((KSCCOLOR)img[0] << 8) | img[1];
            img += 2;
            kfull(&ctx->dev, scr, c, x + ww * s, y + hh * s, s, s);
        }
    }
    return 1;
}

static int cmd_ibin(app_t* app, const char** argv)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (!APPCMD_HAS(argv, 'x') || !APPCMD_HAS(argv, 'y') ||
        !APPCMD_HAS(argv, 'w') || !APPCMD_HAS(argv, 'h') ||
        !APPCMD_HAS(argv, 'c') || !APPCMD_HAS(argv, 'b')) return -1;
    uint16_t x = (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    uint16_t y = (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    uint8_t w = (uint8_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    uint8_t h = (uint8_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    KSCCOLOR fg = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    KSCCOLOR bg = (KSCCOLOR)strtoul(argv[APPCMD_ARG('b')], NULL, 16);
    const uint8_t* img = (const uint8_t*)app->input_data;
    if (!img) return -1;
    kimagebin(&ctx->dev, scr, img, x, y, w, h, fg, bg);
    return 1;
}

static int cmd_drawbmp(app_t* app, const char** argv)
{
    (void)argv;
    gui_ctx_t* ctx = GUI_CTX(app);
    GUI_REQUIRE_HW(ctx);
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;

    app_t* open_app = appget("open");
    if (!open_app) return -1;

    uint8_t header[54];
    int n = appread(open_app, header, sizeof(header), 0);
    if (n < 54) return -1;
    if (header[0] != 'B' || header[1] != 'M') return -1;

    int32_t w = *(int32_t*)&header[18];
    int32_t h = *(int32_t*)&header[22];
    uint16_t bpp = *(uint16_t*)&header[28];
    uint32_t compression = *(uint32_t*)&header[30];
    uint32_t pixel_off = *(uint32_t*)&header[10];

    if (bpp != 24 || compression != 0) return -1;

    int bottom_up = 1;
    if (h < 0) { h = -h; bottom_up = 0; }
    if (w <= 0 || h <= 0 || w > 240 || h > 240) return -1;

    if (pixel_off > sizeof(header)) {
        uint32_t skip = pixel_off - sizeof(header);
        uint8_t tmp[64];
        while (skip > 0) {
            uint32_t rd = skip > sizeof(tmp) ? sizeof(tmp) : skip;
            appread(open_app, tmp, rd, 0);
            skip -= rd;
        }
    }

    uint32_t np = (uint32_t)w * h;
    KSCCOLOR* pixels = (KSCCOLOR*)osmalloc(np * sizeof(KSCCOLOR));
    if (!pixels) return -1;

    int row_pad = ((w * 3 + 3) / 4) * 4;
    uint8_t* row = (uint8_t*)osmalloc(row_pad > 64 ? row_pad : 64);
    if (!row) { osfree(pixels); return -1; }

    for (int y = 0; y < h; y++) {
        int got = appread(open_app, row, row_pad, 0);
        if (got < 0) break;
        int dst_y = bottom_up ? (h - 1 - y) : y;
        KSCCOLOR* dst = pixels + dst_y * w;
        for (int x = 0; x < w && x * 3 + 2 < got; x++) {
            uint8_t b = row[x * 3];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            dst[x] = (KSCCOLOR)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }

    osfree(row);
    kdrawimage(&ctx->dev, scr, pixels, 0, 0, (uint8_t)w, (uint8_t)h);
    osfree(pixels);

    return 1;
}

/* ================================================================
 * appcmd dispatcher (SHARED)
 ================================================================ */
typedef struct { const char* name; int (*handler)(app_t*, const char**); } gui_appcmd_t;

#if __USE_APP_HELP__
static int cmd_help(app_t* app, const char** argv)
{
    (void)argv;
    if (app->output_fn) {
        const char* text =
            "Drawing:\r\n"
            "  pixel -x <x> -y <y> -c <color>\r\n"
            "  fill -x <x> -y <y> -w <w> -h <h> -c <color>\r\n"
            "  rect -x <x> -y <y> -w <w> -h <h> -c <color>\r\n"
            "  line -x <x> -y <y> -w <w> -z <z> -c <color>\r\n"
            "  char -x <x> -y <y> -v <ascii> -c <fg> [-b <bg>]\r\n"
            "  string -x <x> -y <y> -s <text> -c <fg> [-b <bg>]\r\n"
            "  circle -x <cx> -y <cy> -r <r> -c <color>\r\n"
            "  fcircle -x <cx> -y <cy> -r <r> -c <color>\r\n"
            "  arc -x <cx> -y <cy> -r <r> -d <dir> -c <color>\r\n"
            "  rrect -x <x> -y <y> -w <w> -h <h> -r <r> -c <color>\r\n"
            "  frrect -x <x> -y <y> -w <w> -h <h> -r <r> -c <color>\r\n"
            "Tiles:\r\n"
            "  wcreate -x <x> -y <y> -w <w> -h <h> -c <bg>\r\n"
            "  wdelete -t <handle>  wselect -t <handle>\r\n"
            "  whide -t <handle>  wshew -t <handle>\r\n"
            "  wtoggle -t <handle>\r\n"
            "  wmove -t <handle> -x <x> -y <y>\r\n"
            "  wresize -t <handle> -w <w> -h <h>\r\n"
            "  wbk -t <handle> -c <color>\r\n"
            "  wzorder -t <handle> -z <z>\r\n"
            "  wactive  winfo -t <handle>  wenum\r\n"
            "  wclear -t <handle>  clear -c <color>\r\n"
            "  setobjpool -t <handle> -n <count>\r\n"
            "  getobjpool -t <handle>\r\n"
            "  setdrawfunc -i <index>\r\n"
            "Rendering:\r\n"
            "  trenderall  tredraw -t <handle>  trender -t <handle>\r\n"
            "Images:\r\n"
            "  drawbmp  ibig -x <x> -y <y> -w <w> -h <h> -s <scale>\r\n"
            "  ibin -x <x> -y <y> -w <w> -h <h> -c <fg> -b <bg>\r\n";
        app->output_fn(text, strlen(text), app->output_ctx);
    }
    return 0;
}
#endif

static const gui_appcmd_t gui_appcmds[] = {
    {"init",        cmd_init},
    {"pixel",       cmd_pixel},
    {"fill",        cmd_fill},
    {"rect",        cmd_rect},
    {"line",        cmd_line},
    {"char",        cmd_char},
    {"string",      cmd_string},
#if __DRAW_CIRCLE__
    {"circle",      cmd_circle},
    {"fcircle",     cmd_fcircle},
    {"arc",         cmd_arc},
    {"rrect",       cmd_rrect},
    {"frrect",      cmd_frrect},
#endif
    {"wclear",      cmd_wclear},
    {"clear",       cmd_clear},
    {"wcreate",     cmd_wcreate},
    {"wdelete",     cmd_wdelete},
    {"wselect",     cmd_wselect},
    {"whide",       cmd_whide},
    {"wshew",       cmd_wshew},
    {"wtoggle",     cmd_wtoggle},
    {"wmove",       cmd_wmove},
    {"wresize",     cmd_wresize},
    {"wbk",         cmd_wbk},
    {"wzorder",     cmd_wzorder},
    {"wactive",     cmd_wactive},
    {"winfo",       cmd_winfo},
    {"wenum",       cmd_wenum},
    {"trenderall",  cmd_trenderall},
    {"tredraw",     cmd_tredraw},
    {"trender",     cmd_trender},
    {"setobjpool",  cmd_setobjpool},
    {"getobjpool",  cmd_getobjpool},
    {"setdrawfunc", cmd_setdrawfunc},
    {"drawbmp",     cmd_drawbmp},
#if __USE_STM32__
    {"drawrow",     cmd_drawrow},
    {"image",       cmd_image},
#endif
    {"ibig",        cmd_ibig},
    {"ibin",        cmd_ibin},
#if __USE_APP_HELP__
    {"help",        cmd_help},
#endif
    {NULL, NULL}
};

static int gui_cmd(app_t* app, const char* cmd, const char** argv)
{
    for (const gui_appcmd_t* e = gui_appcmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

/* ================================================================
 * appwrite — fast path for ksc_obj_t rendering
 * ================================================================ */

static int gui_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    gui_ctx_t* ctx = GUI_CTX(app);
    if (!ctx || !ctx->hw_inited || !ctx->active_handle) return 0;
    KSC_window* scr = &ctx->tiles[ctx->active_slot].win;
    if (mode == 1 && data)
        kobjdraw(&ctx->dev, scr, (ksc_obj_t*)data);
    else if (mode == 2 && data)
        kobjsdraw(&ctx->dev, scr, (ksc_obj_t*)data, (uint8_t)count);
    return (int)count;
}

/* ================================================================
 * App descriptor (PLATFORM SPECIFIC)
 * ================================================================ */
#if __USE_STM32__
static const papp_ops_t kscgui_ops = {
    .open  = gui_open,
    .close = gui_close,
    .read  = NULL,
    .write = gui_write,
    .cmd   = gui_cmd,
};

REGISTER_APP_EX("KSCGUI", "0", "1\0super_spi", &kscgui_ops,
    "KSC GUI Manager (Tile-based, 16 slots, Z-order compositing)");
#elif __USE_PC__
static const papp_ops_t kscgui_ops = {
    .open  = gui_open,
    .close = gui_close,
    .read  = NULL,
    .write = gui_write,
    .cmd   = gui_cmd,
};

REGISTER_APP("KSCGUI", "0", &kscgui_ops,
    "KSC GUI Manager (Tile-based, 16 slots, Z-order compositing)");
#endif
