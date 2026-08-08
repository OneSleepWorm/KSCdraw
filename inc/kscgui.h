#ifndef KSCGUI_H
#define KSCGUI_H

#include "app.h"
#include "KSCdraw.h"

/* tile_h_t 定义于 apps/app_config.h (与其它 app 共享) */
#include "../apps/app_config.h"

/* ================================================================
 * KSCGUI 共享类型与接口 — 跨平台 (PC/STM32)。
 * 平台驱动 (gui_drv) 通过 gui_ctx_t.drv (void*) 访问平台私有上下文,
 * 平台 ops 经 dev.data (=gui_ctx_t*) 访问。
 * ================================================================ */

/* Constants */
#define KSCGUI_TFT_W       240
#define KSCGUI_TFT_H       320
#define KSCGUI_TILE_MAX    16

/* Tile flags */
#define KSCGUI_TILE_F_USED     0x01
#define KSCGUI_TILE_F_VISIBLE  0x02

/* Handle encoding */
#define KSCGUI_TILE_SLOT(h)            ((h) & 0x0F)
#define KSCGUI_TILE_GEN(h)             ((h) >> 4)
#define KSCGUI_TILE_MAKE_HANDLE(g,s)   ((tile_h_t)(((uint8_t)(g) << 4) | (uint8_t)(s)))

typedef struct {
    KSC_window  win;         /* ssx/ssy/width/height/bk/objbuf/objnum/Mode */
    uint8_t     gen;         /* generation 1..15, 0=slot free */
    uint8_t     flags;       /* USED | VISIBLE */
    uint8_t     z;           /* Z-order, higher = drawn later = on top */
} kscgui_tile_t;

typedef struct {
    k_draw_device   dev;            /* 渲染 ops 表 (平台填充) */
    kscgui_tile_t   tiles[KSCGUI_TILE_MAX];
    uint16_t        tile_free_map;  /* bitmap: 1=slot free */
    tile_h_t        active_handle;  /* 0=none */
    uint8_t         active_slot;
    uint8_t         hw_inited;      /* 1=cmd_init 已完成屏幕初始化 */
    void*           drv;            /* 平台驱动上下文 (gui_drv 私有) */
} kscgui_ctx_t;

/* GUI_CTX cast helper */
#define KSCGUI_CTX(app) ((kscgui_ctx_t*)(app)->app_data)

/* 绘图命令前置检查: 屏幕必须已 init 且存在 active tile */
#define KSCGUI_REQUIRE_HW(ctx) \
    do { if (!(ctx) || !(ctx)->hw_inited || !(ctx)->active_handle) return -1; } while (0)

/* ================================================================
 * 平台驱动接口 (bsp/<平台>/gui_drv.c 实现)
 * 注: PC gui_drv 按 C++ 编译 (easyx), 需 extern "C" 保证与 C 侧链接一致。
 * ================================================================ */
#ifdef __cplusplus
extern "C" {
#endif
int  kscgui_drv_open(kscgui_ctx_t* ctx, app_t* app);   /* 填 dev ops + 平台初始化 */
void kscgui_drv_close(kscgui_ctx_t* ctx);              /* 平台释放 */
#ifdef __cplusplus
}
#endif

#endif /* KSCGUI_H */
