/**
 * @file    gui_drv.c
 * @note    KSCGUI 平台驱动 — Linux (SDL2)
 *
 * 实现 kscgui.h 声明的 kscgui_drv_open/close。自填 k_draw_device ops 表
 * (SDL2 渲染 + init 建窗), 不依赖共享层的系统设备。
 *
 * ============================================================
 * 渲染策略: RGB565 后台帧缓冲 + 批量 present
 * ============================================================
 * PC (easyx) 版对每个像素调用一次 solidrectangle(), 全屏刷新是 76800 次
 * GDI 调用。本实现改为:
 *
 *   setcanvas       设置写入窗口与流式游标 (沿用 PC 版游标模型)
 *   setcolorpixels  按游标顺序纯内存写入 framebuffer (O(n), 无系统调用)
 *   kscgui_sdl_pump SDL_UpdateTexture + RenderCopy + RenderPresent 一次
 *
 * KSCCOLOR 是 uint16_t RGB565, 与 SDL_PIXELFORMAT_RGB565 的 packed16
 * 布局 (RRRRRGGGGGGBBBBB) 同构, 故无需逐像素颜色转换 (PC 版需要
 * color16to24, 是其开销大头之一)。
 *
 * 缩放由 SDL_RenderCopy 把 240x320 纹理拉伸到窗口客户区实现, 零额外成本;
 * SDL_HINT_RENDER_SCALE_QUALITY=0 保持像素锐利 (最近邻)。
 *
 * ============================================================
 * 事件泵
 * ============================================================
 * SDL 窗口必须周期性 SDL_PollEvent 才不假死/可关闭。kscgui_sdl_pump()
 * 由 bsp/linux/uart_serial.c 的 appread() 调用 —— 主循环 kscterminal()
 * 每轮都会 appread(ksc_console, ...), 因此泵在每轮空转都被驱动,
 * 无需改动三平台共享的 src/main.c / src/KSCOSsystem.c。
 * sys_idle() (bsp/linux/system.c) 亦会调用, 保证长延时期间仍响应。
 *
 * 键盘状态经 kscgui_sdl_key_down(i) 暴露给 bsp/linux/button16.c。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscgui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* KSCconfig.h 在 __USE_PC__ 下定义了日志宏 `#define log(...) 0` (被 -include 强制
 * 注入到本 TU 开头), 与 <math.h> 的 log() 冲突 —— 而 SDL2 会间接包含 math.h。
 * 本 TU 不使用该日志宏, 包含 SDL 之前先撤销。 */
#undef log

#include <SDL2/SDL.h>

#define SCALE KSC_PC_SCALE

#define SCR_W   KSCGUI_TFT_W   /* 240 */
#define SCR_H   KSCGUI_TFT_H   /* 320 */

typedef struct {
    SDL_Window*   win;
    SDL_Renderer* rend;
    SDL_Texture*  tex;
    KSCCOLOR*     fb;      /* SCR_W * SCR_H RGB565 后台帧缓冲 (osmalloc) */
    int           dirty;   /* 本轮是否有像素写入, 决定是否 present */
} sdl_gui_ctx_t;

/* ── 流式写入游标 (语义与 PC 版一致) ── */
static uint16_t sSx, sSy, sEx, sEy, sCx, sCy;

/* ── 4x4 矩阵键盘: 键位索引 → SDL scancode ──
 * 布局与 bsp/pc/button16.c 保持一致 ('1'..'4' / 'Q''W''E''R' /
 * 'A''S''D''F' / 'Z''X''C''V')。 */
static const int s_key_scancodes[16] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V
};

static sdl_gui_ctx_t* s_ctx = NULL;   /* 当前活动 GUI 上下文 (单例) */

/* 前向声明: gui_hw_init 需要在定义前调用泵以呈现首帧 */
void kscgui_sdl_pump(void);
int  kscgui_sdl_key_down(int idx);

static void movecursor(void)
{
    sCx++;
    if (sCx > sEx) {
        sCx = sSx;
        sCy++;
    }
}

/* ================================================================
 *  k_draw_device ops
 * ================================================================ */
static void gui_hw_init(void* data)
{
    (void)data;
    sdl_gui_ctx_t* c = s_ctx;
    if (!c || c->win) return;   /* 已建窗则幂等 */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[gui_drv] SDL_Init failed: %s\r\n", SDL_GetError());
        return;
    }
    /* 最近邻缩放, 保持像素锐利 (必须在创建 renderer 之前设置) */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    c->win = SDL_CreateWindow("KSCOS",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCR_W * SCALE, SCR_H * SCALE,
                              SDL_WINDOW_SHOWN);
    if (!c->win) {
        fprintf(stderr, "[gui_drv] SDL_CreateWindow failed: %s\r\n", SDL_GetError());
        return;
    }

    c->rend = SDL_CreateRenderer(c->win, -1,
                                 SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!c->rend) {
        /* 硬件加速不可用时退回软件渲染 (无显示器/虚拟机环境常见) */
        c->rend = SDL_CreateRenderer(c->win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!c->rend) {
        fprintf(stderr, "[gui_drv] SDL_CreateRenderer failed: %s\r\n", SDL_GetError());
        return;
    }

    c->tex = SDL_CreateTexture(c->rend, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    if (!c->tex) {
        fprintf(stderr, "[gui_drv] SDL_CreateTexture failed: %s\r\n", SDL_GetError());
        return;
    }

    /* 首帧全黑, 避免未绘制区域显示脏数据 */
    memset(c->fb, 0, (size_t)SCR_W * SCR_H * sizeof(KSCCOLOR));
    c->dirty = 1;
    kscgui_sdl_pump();
}

static void gui_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    (void)data;
    sSx = Gx; sSy = Gy; sEx = Gx + width - 1; sEy = Gy + height - 1;
    sCx = Gx; sCy = Gy;
}

/* 热路径: 每帧可达数万次调用, 保持纯内存写入, 禁止日志/系统调用 */
static void gui_pixels(void* data, const KSCCOLOR* colors, uint16_t num)
{
    (void)data;
    sdl_gui_ctx_t* c = s_ctx;
    if (!c || !c->fb) return;

    while (num--) {
        KSCCOLOR ncolor = *colors++;
        /* 越界保护: 游标可能跑出画布 (画布尺寸 > 屏幕时) */
        if (sCx < SCR_W && sCy < SCR_H) {
            c->fb[(size_t)sCy * SCR_W + sCx] = ncolor;
            c->dirty = 1;
        }
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

/* ================================================================
 *  SDL 事件泵 (供 uart_serial / system 周期性调用)
 * ================================================================ */
void kscgui_sdl_pump(void)
{
    sdl_gui_ctx_t* c = s_ctx;
    if (!c || !c->rend) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            /* 优雅退出: 先冲刷文件通道, 再退出进程 */
            fflush(NULL);
            SDL_Quit();
            exit(0);
        }
    }

    if (!c->dirty) return;
    c->dirty = 0;

    SDL_UpdateTexture(c->tex, NULL, c->fb, SCR_W * (int)sizeof(KSCCOLOR));
    SDL_RenderClear(c->rend);
    SDL_RenderCopy(c->rend, c->tex, NULL, NULL);
    SDL_RenderPresent(c->rend);
}

/* button16.c 查询 4x4 键位状态; idx = 0..15 */
int kscgui_sdl_key_down(int idx)
{
    if (idx < 0 || idx > 15) return 0;
    if (!s_ctx || !s_ctx->win) return 0;
    const Uint8* st = SDL_GetKeyboardState(NULL);
    return st[s_key_scancodes[idx]] ? 1 : 0;
}

/* ================================================================
 *  平台驱动契约 (inc/kscgui.h)
 * ================================================================ */
int kscgui_drv_open(kscgui_ctx_t* ctx, app_t* app)
{
    (void)app;
    sdl_gui_ctx_t* c = (sdl_gui_ctx_t*)osmalloc(sizeof(sdl_gui_ctx_t));
    if (!c) return -1;
    memset(c, 0, sizeof(*c));

    /* 帧缓冲 240*320*2 = 150KB — 用宿主机 malloc, 不走 osmalloc/mempool。
     *
     * 项目约定"运行期上下文一律 osmalloc"是为 STM32 服务的 (1KB 栈 / 20KB RAM,
     * 无 libc 堆, mempool 最大档仅 2048B 且超档直接返回 NULL)。本文件是
     * Linux 专属 BSP, 150KB 帧缓冲不可能塞进 2KB 块池, 故直接使用宿主堆 —— 
     * 这也是 PC (easyx) 版图形缓冲由 easyx 内部管理的等价做法。 */
    c->fb = (KSCCOLOR*)malloc((size_t)SCR_W * SCR_H * sizeof(KSCCOLOR));
    if (!c->fb) { free(c); return -1; }

    ctx->dev.data = ctx;
    ctx->dev.init = gui_hw_init;
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;
    ctx->dev.setpixels = gui_window_setpixels;
    kobjdraw_init(&ctx->dev);

    ctx->drv = c;
    s_ctx = c;
    return 0;
}

void kscgui_drv_close(kscgui_ctx_t* ctx)
{
    sdl_gui_ctx_t* c = (sdl_gui_ctx_t*)ctx->drv;
    if (!c) return;

    if (c->tex) SDL_DestroyTexture(c->tex);
    if (c->rend) SDL_DestroyRenderer(c->rend);
    if (c->win) SDL_DestroyWindow(c->win);
    if (c->fb) free(c->fb);
    free(c);

    ctx->drv = NULL;
    if (s_ctx == c) s_ctx = NULL;
    SDL_Quit();
}
