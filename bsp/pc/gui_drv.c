/**
 * @file    gui_drv.c
 * @note    KSCGUI 平台驱动 — PC (EasyX)
 *
 * 实现 kscgui.h 声明的 kscgui_drv_open/close。自填 k_draw_device ops 表
 * (easyx 渲染 + init 建窗), 不依赖共享层的系统设备。平台无私有上下文。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscgui.h"
#include <string.h>
#include <stdlib.h>
#include "../../third_party/easyx/include/graphics.h"
#include <stdio.h>

#define SCALE KSC_PC_SCALE

static uint32_t color16to24(uint16_t color16)
{
    uint8_t r5 = (color16 >> 11) & 0x1F;
    uint8_t g6 = (color16 >> 5) & 0x3F;
    uint8_t b5 = (color16 & 0x1F);
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    return (b8 << 16) | (g8 << 8) | r8;
}

static uint16_t sSx, sSy, sEx, sEy, sCx, sCy;

static void movecursor(void)
{
    sCx++;
    if (sCx > sEx) {
        sCx = sSx;
        sCy++;
    }
}

static void gui_hw_init(void* data)
{
    (void)data;
    initgraph(TFTx * SCALE, 320 * SCALE);
    setlinecolor(BLACK);
    HWND hwnd = GetHWnd();
    SetWindowPos(hwnd, NULL, 200, 50, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void gui_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    (void)data;
    sSx = Gx; sSy = Gy; sEx = Gx + width - 1; sEy = Gy + height - 1;
    sCx = Gx; sCy = Gy;
}

static void gui_pixels(void* data, const KSCCOLOR* colors, uint16_t num)
{
    (void)data;
    if (GetHWnd() == NULL) return;  /* 未 initgraph 兜底, 防 easyx 崩溃 */
    while (num--) {
        KSCCOLOR ncolor = *colors++;
        setfillcolor(color16to24(ncolor));
        solidrectangle(sCx * SCALE, sCy * SCALE, sCx * SCALE + SCALE, sCy * SCALE + SCALE);
        movecursor();
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

int kscgui_drv_open(kscgui_ctx_t* ctx, app_t* app)
{
    (void)app;
    ctx->dev.data = ctx;
    ctx->dev.init = gui_hw_init;
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;
    ctx->dev.setpixels = gui_window_setpixels;
    kobjdraw_init(&ctx->dev);
    ctx->drv = NULL;
    return 0;
}

void kscgui_drv_close(kscgui_ctx_t* ctx)
{
    ctx->drv = NULL;
}
