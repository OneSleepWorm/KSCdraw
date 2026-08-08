/**
 * @file    snake.c
 * @note    Snake Game — 纯中断驱动, 全对象增量渲染
 * @flash   ~1892B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  snake
 * 依赖:    KSCGUI + button16 + tim_clock
 * 平台:    STM32 (__USE_STM32__)
 *
 * 资源占用 (LTO差分法)
 *   ROM(Debug -O0):   3,164 B
 *   ROM(Release -Os):  1,908 B
 *   RAM(静态):   8 B (spawn_food 内的 static uint32_t rnd)
 *   RAM(堆):    约 1 KB (snake_ctx_t, osmalloc 于 appopen)
 *
 * 用户代码:
 *   app_t* g = appget("snake");
 *   appopen(g);
 *   appcmd(g, "init -m 1");   // 1=阻塞模式, 2=中断模式
 *   appclose(g);
 *
 * ============================================================
 * 按键映射 (计算器布局)
 * ============================================================
 *   5 6 7   ↑   (整排上)
 *   9   11  ← →
 *  13 14 15 ↓   (整排下)
 *   10 暂停  0 退出
 *   game_over 时任意键重新开始
 *
 * ============================================================
 * 内部设计
 * ============================================================
 * 所有逻辑在 TIM4 中断回调中执行 (每 250ms):
 *   1. 读 button16 事件队列 (mode=3, PRESS only)
 *   2. 处理按键 (方向/暂停/退出/重启)
 *   3. 更新游戏状态 (碰撞/食物)
 *   4. 增量渲染 (_dirty 擦除+绘制)
 *   5. SPI DMA 传输 (在 ISR 中同步等待)
 *
 * 主线程: 阻塞模式(snake_init mode=1) → WFI 循环等待 K0
 *          中断模式(snake_init mode=2) → 直接返回, 由 appclose 清理
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "app_config.h"
#include <string.h>
#include <stdio.h>

/* ── 游戏常量 ── */

#define COLS        24
#define ROWS        24
#define CS          10
#define MAX_SNAKE   58
#define TICK_MS     250
#define SCAN_MS     50

enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

/* ── 对象布局 (64 槽) ── */
#define OBJ_WALL     0
#define OBJ_FOOD     1
#define OBJ_SNAKE    2       /* 占用 MAX_SNAKE 槽 */
#define OBJ_SCORE    (2 + MAX_SNAKE)       /* 60 */
#define OBJ_GAMEOVER (3 + MAX_SNAKE)       /* 61 */
#define OBJ_PAUSE    (4 + MAX_SNAKE)       /* 62 */
#define OBJ_ERASE    (5 + MAX_SNAKE)       /* 63 */
#define OBJ_TOTAL    (6 + MAX_SNAKE)       /* 64 */

/* ── 类型 ── */

typedef struct { uint8_t x, y; } pos_t;

typedef struct {
    app_t*  obj;
    app_t*  kpd;
    app_t*  tim;

    ksc_obj_t objs[OBJ_TOTAL];
    char      score_buf[16];

    pos_t     snake[MAX_SNAKE + 1];  /* +1 给 snake[snake_len] 用 */
    uint16_t  snake_len;
    uint8_t   dir;
    uint8_t   next_dir;
    pos_t     food;
    uint16_t  score;
    uint8_t   game_over;
    uint8_t   paused;
    uint8_t   running;
    uint8_t   hw_opened;   /* 1 = kpd/gui/tmr 已启用 */
    uint8_t   tile_h;      /* tile 句柄 */

} snake_ctx_t;

/* ── 游戏逻辑 ── */

static void spawn_food(snake_ctx_t* ctx)
{
    static uint32_t rnd = 1;
    rnd = rnd * 1103515245 + 12345;
    ctx->food.x = (uint8_t)((rnd >> 8) % COLS);
    ctx->food.y = (uint8_t)((rnd >> 16) % ROWS);
}

static void init_game(snake_ctx_t* ctx)
{
    ctx->snake_len = 3;
    ctx->snake[0].x = 12; ctx->snake[0].y = 12;
    ctx->snake[1].x = 11; ctx->snake[1].y = 12;
    ctx->snake[2].x = 10; ctx->snake[2].y = 12;
    ctx->dir       = DIR_RIGHT;
    ctx->next_dir  = DIR_RIGHT;
    ctx->score     = 0;
    ctx->game_over = 0;
    ctx->paused    = 0;
    ctx->food.x    = 18;
    ctx->food.y    = 12;
}

static void update_game(snake_ctx_t* ctx)
{
    if (ctx->game_over || ctx->paused) return;

    ctx->dir = ctx->next_dir;

    pos_t nh = ctx->snake[0];
    switch (ctx->dir) {
    case DIR_UP:    if (nh.y == 0)          { ctx->game_over = 1; return; } nh.y--; break;
    case DIR_DOWN:  if (nh.y >= ROWS - 1)   { ctx->game_over = 1; return; } nh.y++; break;
    case DIR_LEFT:  if (nh.x == 0)          { ctx->game_over = 1; return; } nh.x--; break;
    case DIR_RIGHT: if (nh.x >= COLS - 1)   { ctx->game_over = 1; return; } nh.x++; break;
    }

    for (uint16_t i = ctx->snake_len; i > 0; i--)
        ctx->snake[i] = ctx->snake[i - 1];
    ctx->snake[0] = nh;

    for (uint16_t i = 1; i < ctx->snake_len; i++) {
        if (ctx->snake[0].x == ctx->snake[i].x &&
            ctx->snake[0].y == ctx->snake[i].y) {
            ctx->game_over = 1;
            return;
        }
    }

    if (ctx->snake[0].x == ctx->food.x && ctx->snake[0].y == ctx->food.y) {
        if (ctx->snake_len < MAX_SNAKE) {
            ctx->snake_len++;
            ctx->score++;
        }
        spawn_food(ctx);
    }
}

/* ── 对象初始化 ── */

static void init_objects(snake_ctx_t* ctx)
{
    ctx->objs[OBJ_WALL].sdx     = 0;
    ctx->objs[OBJ_WALL].sdy     = 0;
    ctx->objs[OBJ_WALL].width   = COLS * CS;
    ctx->objs[OBJ_WALL].height  = ROWS * CS;
    ctx->objs[OBJ_WALL].colorck = 0x39E7;
    ctx->objs[OBJ_WALL]._type   = _box;

    ctx->objs[OBJ_FOOD].width   = CS;
    ctx->objs[OBJ_FOOD].height  = CS;
    ctx->objs[OBJ_FOOD].colorck = 0xF800;
    ctx->objs[OBJ_FOOD]._type   = _fillbox;

    for (uint16_t i = 0; i < MAX_SNAKE; i++) {
        ctx->objs[OBJ_SNAKE + i].width  = CS;
        ctx->objs[OBJ_SNAKE + i].height = CS;
        ctx->objs[OBJ_SNAKE + i]._type  = _fillbox;
    }

    ctx->objs[OBJ_SCORE].sdx     = 4;
    ctx->objs[OBJ_SCORE].sdy     = 245;
    ctx->objs[OBJ_SCORE].width   = 120;
    ctx->objs[OBJ_SCORE].height  = 10;
    ctx->objs[OBJ_SCORE].colorck = 0xFFFF;
    ctx->objs[OBJ_SCORE].data    = ctx->score_buf;
    ctx->objs[OBJ_SCORE]._type   = _string;

    ctx->objs[OBJ_GAMEOVER].sdx     = 60;
    ctx->objs[OBJ_GAMEOVER].sdy     = 110;
    ctx->objs[OBJ_GAMEOVER].width   = 120;
    ctx->objs[OBJ_GAMEOVER].height  = 10;
    ctx->objs[OBJ_GAMEOVER].colorck = 0xF800;
    ctx->objs[OBJ_GAMEOVER].data    = (void*)"GAME OVER";
    ctx->objs[OBJ_GAMEOVER]._type   = _string;

    ctx->objs[OBJ_PAUSE].sdx     = 60;
    ctx->objs[OBJ_PAUSE].sdy     = 130;
    ctx->objs[OBJ_PAUSE].width   = 120;
    ctx->objs[OBJ_PAUSE].height  = 10;
    ctx->objs[OBJ_PAUSE].colorck = 0xFFFF;
    ctx->objs[OBJ_PAUSE].data    = (void*)"PAUSED";
    ctx->objs[OBJ_PAUSE]._type   = _string;

    ctx->objs[OBJ_ERASE].width   = CS;
    ctx->objs[OBJ_ERASE].height  = CS;
    ctx->objs[OBJ_ERASE].colorck = 0x0000;
    ctx->objs[OBJ_ERASE]._type   = _fillbox;
}

/* ── 渲染(全量) ── */

static void render(snake_ctx_t* ctx, uint8_t ate, uint8_t first_frame)
{
    uint16_t i;

    /* 擦除器: 仅在非首帧且未吃食物时设置 */
    if (!first_frame && !ate && ctx->snake_len > 0 &&
        ctx->snake_len < MAX_SNAKE + 1) {
        ctx->objs[OBJ_ERASE].sdx    = ctx->snake[ctx->snake_len].x * CS;
        ctx->objs[OBJ_ERASE].sdy    = ctx->snake[ctx->snake_len].y * CS;
        ctx->objs[OBJ_ERASE].width  = CS;
        ctx->objs[OBJ_ERASE].height = CS;
        ctx->objs[OBJ_ERASE]._type  = _fillbox;
    } else {
        ctx->objs[OBJ_ERASE]._type  = 0xFF;  /* 无效索引, 跳过绘制 */
    }

    /* 蛇身 */
    for (i = 0; i < ctx->snake_len && i < MAX_SNAKE; i++) {
        uint16_t idx = OBJ_SNAKE + i;
        ctx->objs[idx].sdx     = ctx->snake[i].x * CS;
        ctx->objs[idx].sdy     = ctx->snake[i].y * CS;
        ctx->objs[idx].colorck = (i == 0) ? 0x07E0 : 0x0540;
        ctx->objs[idx]._type   = _fillbox;
    }

    /* 食物 */
    ctx->objs[OBJ_FOOD].sdx   = ctx->food.x * CS;
    ctx->objs[OBJ_FOOD].sdy   = ctx->food.y * CS;
    ctx->objs[OBJ_FOOD]._type = _fillbox;

    /* 分数 */
    snprintf(ctx->score_buf, sizeof(ctx->score_buf), "Score: %u", ctx->score);
    ctx->objs[OBJ_SCORE]._type = _string;

    /* GAME OVER / PAUSED */
    if (ctx->game_over)
        ctx->objs[OBJ_GAMEOVER]._type = _string;
    else if (ctx->paused)
        ctx->objs[OBJ_PAUSE]._type = _string;
    else {
        ctx->objs[OBJ_GAMEOVER]._type = 0xFF;
        ctx->objs[OBJ_PAUSE]._type    = 0xFF;
    }
}

/* ── 全量擦除 ── */

static void render_full_reset(snake_ctx_t* ctx)
{
    ctx->objs[OBJ_ERASE].sdx     = 0;
    ctx->objs[OBJ_ERASE].sdy     = 0;
    ctx->objs[OBJ_ERASE].width   = COLS * CS;
    ctx->objs[OBJ_ERASE].height  = ROWS * CS;
    ctx->objs[OBJ_ERASE]._type   = _fillbox;
}

/* ── 重现初始状态 (重启/首次) ── */

static void show_initial(snake_ctx_t* ctx)
{
    /* pass 1: 全量擦除 + 画静态元素 (墙/分数) */
    render_full_reset(ctx);
    appwrite(ctx->obj, ctx->objs, OBJ_TOTAL, 0x02);

    /* pass 2: 画蛇/食物 (首帧, 无擦除器) */
    render(ctx, 0, 1);
    appwrite(ctx->obj, ctx->objs, OBJ_TOTAL, 0x02);
}

/* ── TIM4 中断回调 ── */

static void* tick_cb(void* data)
{
    app_t* app = (app_t*)data;
    snake_ctx_t* ctx = (snake_ctx_t*)app->app_data;
    if (!ctx->running) return NULL;

    /* 读 button16 事件队列 (mode=3), 只处理 PRESS */
    uint32_t ev;
    while (appread(ctx->kpd, &ev, 0, 3) > 0) {
        uint8_t key = (ev >> 4) & 0xF;
        uint8_t type = ev & 0xF;
        if (type != 0) continue;

        /* K0 退出: 停止 TIM4, mode=2 时上层通过 appread 感知 */
        if (key == 0) {
            ctx->running = 0;
            appwrite(ctx->tim, NULL, 0, 0x42);
            return NULL;
        }

        /* game_over: 任意键重启 */
        if (ctx->game_over) {
            init_game(ctx);
            init_objects(ctx);
            ctx->obj->mode_data = (void*)(uintptr_t)ctx->tile_h;
            ctx->obj->input_data = ctx->objs;
            { char _b[32]; snprintf(_b, sizeof(_b), "setobjpool -n %d", OBJ_TOTAL); appcmd(ctx->obj, _b); }
            show_initial(ctx);
            return NULL;
        }

        /* K10 暂停切换 */
        if (key == 10) {
            ctx->paused = !ctx->paused;
            if (!ctx->paused) {
                /* 取消暂停: 全量擦除后重绘 (PAUSED 文字不再残留) */
                render_full_reset(ctx);
                appwrite(ctx->obj, ctx->objs, OBJ_TOTAL, 0x02);
                render(ctx, 0, 1);
                appwrite(ctx->obj, ctx->objs, OBJ_TOTAL, 0x02);
                return NULL;
            }
            continue;
        }

        /* 方向 (计算器布局) */
        if ((key == 5 || key == 6 || key == 7) && ctx->dir != DIR_DOWN)
            ctx->next_dir = DIR_UP;
        if (key == 9  && ctx->dir != DIR_RIGHT)
            ctx->next_dir = DIR_LEFT;
        if (key == 11 && ctx->dir != DIR_LEFT)
            ctx->next_dir = DIR_RIGHT;
        if ((key == 13 || key == 14 || key == 15) && ctx->dir != DIR_UP)
            ctx->next_dir = DIR_DOWN;
    }

    /* 游戏 tick */
    if (!ctx->paused) {
        uint16_t len_before = ctx->snake_len;
        update_game(ctx);
        uint8_t ate = (ctx->snake_len > len_before) ? 1 : 0;
        render(ctx, ate, 0);
    } else {
        render(ctx, 0, 0);
    }

    appwrite(ctx->obj, ctx->objs, OBJ_TOTAL, 0x02);
    return NULL;
}

/* ── App 生命周期 ── */

static int snake_open(app_t* app)
{
    snake_ctx_t* ctx = (snake_ctx_t*)osmalloc(sizeof(snake_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(snake_ctx_t));
    ctx->obj = app->app0;
    ctx->kpd = app->app1;
    ctx->tim = appget("tim_clock");
    app->app_data = ctx;
    return 0;
}

static int snake_close(app_t* app)
{
    snake_ctx_t* ctx = (snake_ctx_t*)app->app_data;
    if (!ctx) return 0;
    ctx->running = 0;
    if (ctx->hw_opened) {
        appwrite(ctx->tim, NULL, 0, 0x42);  /* stop TIM4 */
        appclose(ctx->kpd);                  /* stops TIM3, frees keypad */
        appclose(ctx->obj);                  /* closes SPI */
    }
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

static int snake_init(app_t* app, int mode)
{
    snake_ctx_t* ctx = (snake_ctx_t*)app->app_data;
    if (ctx->hw_opened) return -1;

    /* GUI */
    if (appopen(ctx->obj) < 0) return -1;
    appcmd(ctx->obj, "init");
    sysdelay(10);
    appcmd(ctx->obj, "wcreate -x 0 -y 0 -w 240 -h 320 -c 0000");
    ctx->tile_h = (uint8_t)(uintptr_t)ctx->obj->output_data;
    ctx->obj->mode_data = (void*)(uintptr_t)ctx->tile_h;
    appcmd(ctx->obj, "wselect");

    /* button16 */
    if (appopen(ctx->kpd) < 0) { appclose(ctx->obj); return -1; }
    appwrite(ctx->kpd, NULL, 0, 1);
    { uint32_t iv = SCAN_MS; appwrite(ctx->kpd, &iv, 1, 2); }

    /* 游戏初始化 + 首次绘制 */
    init_game(ctx);
    init_objects(ctx);
    ctx->obj->mode_data = (void*)(uintptr_t)ctx->tile_h;
    ctx->obj->input_data = ctx->objs;
    { char _b[32]; snprintf(_b, sizeof(_b), "setobjpool -n %d", OBJ_TOTAL); appcmd(ctx->obj, _b); }
    show_initial(ctx);

    /* TIM4: mode=0x41 设周期, mode=0x42 count=1 启动 */
    ctx->tim->user_func  = tick_cb;
    ctx->tim->input_data = app;
    appopen(ctx->tim);
    appwrite(ctx->tim, NULL, TICK_MS, 0x41);
    appwrite(ctx->tim, NULL, 1,      0x42);

    ctx->hw_opened = 1;
    ctx->running   = 1;

    if (mode == 1) {
        /* 阻塞模式: 等待 K0 退出 */
        while (ctx->running)
            oswait_idle();
    }
    /* mode=2 (中断模式): 直接返回, 游戏在 TIM4 ISR 中运行 */

    return 0;
}

static int snake_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)count;
    snake_ctx_t* ctx = (snake_ctx_t*)app->app_data;
    if (!ctx) return 0;
    if (mode == 1 && data) {
        *(uint32_t*)data = ctx->running ? 1 : 0;
        return 4;
    }
    return 0;
}

static int snake_cmd(app_t* app, const char* cmdname, const char** argv)
{
    snake_ctx_t* ctx = (snake_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (strcmp(cmdname, "init") == 0) {
        int mode = APPCMD_HAS(argv, 'm') ? (int)strtoul(argv[APPCMD_ARG('m')], NULL, 0) : 2;
        return snake_init(app, mode);
    }
    return -1;
}

static const papp_ops_t snake_ops = {
    .open   = snake_open,
    .close  = snake_close,
    .read   = snake_read,
    .write  = NULL,
    .cmd    = snake_cmd,
};

/* dep: none (tim_clock managed internally), app_dep: KSCGUI(app0), button16(app1) */
REGISTER_APP_EX("snake", "0", "2\0KSCGUI\0button16",
                &snake_ops, "Snake Game (interrupt-driven, incremental)");
