/**
 * @file    list.c
 * @note    GUI List Widget — 字符串池 + 碎片管理 + 多选中样式 + 回调
 * @flash   ~3132B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  list
 * 依赖:    KSCGUI
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用
 * ============================================================
 * ROM(Debug -O0):  ~2000 B
 * ROM(Release -Os): ~1000 B
 * RAM(静态): 0 B
 * RAM(堆):  约 530 B (list_ctx_t)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * 全部通过 appcmd 接口:
 *
 *   appget("list") -> app_t*
 *   appopen(list)  -> 分配 context, 设默认值
 *   appclose(list) -> 释放
 *
 *   --- 数据操作 ---
 *   appcmd(list, "add -d label")           -> 追加一项
 *   appcmd(list, "remove -i idx")          -> 删除一项
 *   appcmd(list, "clear")                  -> 清空
 *   appcmd(list, "compact")                -> 手动碎片整理
 *
 *   --- 选中 ---
 *   appcmd(list, "select -i idx")          -> 选中指定项
 *   appcmd(list, "move -d delta")          -> [+1/-1] 移动选中
 *   appcmd(list, "confirm")                -> 触发 user_func
 *   选中变化时自动调用 app->user_func(app->input_data)
 *
 *   --- 查询 ---
 *   appcmd(list, "getcount")               -> int 返回值
 *   appcmd(list, "getlabel -i idx")        -> const char* 零拷贝指针
 *   appread(list, &sel, 0, 1)             -> 读 selected
 *   appread(list, buf, idx, 2)            -> 拷贝标签
 *
 *   --- 外观 ---
 *   appcmd(list, "setpos") [input_data]     -> list_pos_t 结构体
 *   appcmd(list, "setcolors") [input_data]  -> list_colors_t 结构体
 *   appcmd(list, "setstyle -s <0-4>")     -> 选中样式 (0-4)
 *   appcmd(list, "init")                   -> 创建 KSCGUI 窗口 + 首绘
 *   appcmd(list, "refresh")                -> 全量重绘
 *
 * ============================================================
 * 选中样式 (list.h)
 * ============================================================
 *   LIST_STYLE_NONE      0   无指示
 *   LIST_STYLE_FILLROW   1   全行填 sel_bg
 *   LIST_STYLE_FILLBAR   2   左侧 4px 竖条
 *   LIST_STYLE_TEXTONLY  3   仅变字色 (sel_fg)
 *   LIST_STYLE_ARROW     4   箭头 ">" 光标 (pad 12px)
 *
 * ============================================================
 * 渲染策略
 * ============================================================
 * 全量渲染 (list_render):
 *   1. appcmd fill 清空可视区域 (防字符串残留)
 *   2. appwrite 0x02 画所有文字
 *   3. 根据 sel_style 画选中行高亮
 *
 * 增量渲染 (highlight_row):
 *   选中行变化时仅刷新两行，每行先清后画
 *
 * ============================================================
 * 字符串池
 * ============================================================
 * data_buf[256] + uint8_t offsets[32] + frags[8]:
 *   add:        碎片 first-fit -> 尾部 -> compact -> 重试
 *   remove:     索引坍缩, 释放区间记入碎片表 (邻接合并)
 *   compact:    memmove 紧缩, 清空碎片表
 *   尾部直缩:   释放区间恰在 data_used 末尾时直接 data_used-=sz
 *
 * ============================================================
 * 典型使用
 * ============================================================
 *
 *   app_t* list = appget("list");
 *   list->user_func = my_cb;
 *   list->input_data = my_ctx;
 *   appopen(list);
 *
 *   appcmd(list, "add -d System");
 *   appcmd(list, "add -d Settings");
 *   appcmd(list, "add -d Media");
 *
 *   list_pos_t pos = {0, 0, 240, 320, 24};
 *   list_colors_t col = {0x001F, 0x0000, 0xFFFF, 0xF800};
 *   list->input_data = &pos; appcmd(list, "setpos");
 *   list->input_data = &col; appcmd(list, "setcolors");
 *   appcmd(list, "setstyle -s 1");
 *   appcmd(list, "init");
 *
 *   appcmd(list, "move -d 1");
 *   uint32_t sel;
 *   appread(list, &sel, 0, 1);
 *
 *   appclose(list);
 *
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "../inc/KSCfont.h"
#include "app_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if __USE_STM32__ || __USE_PC__

/* ── 常量 ── */

#define LIST_DATA_BUF     256
#define LIST_MAX_ITEMS    32
#define LIST_MAX_VISIBLE  16
#define LIST_ITEM_H_DEF   20
#define LIST_FRAG_MAX     8
#define LIST_FRAG_MIN     8

#define CTRL_INTERVAL_MS  20
#define CTRL_HOLD_TICKS   25
#define CTRL_HOLD_GAP     6

/* ── 碎片 ── */

typedef struct {
    uint8_t off;
    uint8_t sz;
} list_frag_t;

/* ── Context ── */

typedef struct {
    app_t*      app;
    app_t*      gui;
    ksc_obj_t   objs[LIST_MAX_VISIBLE];

    char        data_buf[LIST_DATA_BUF];
    uint8_t     offsets[LIST_MAX_ITEMS];
    list_frag_t frags[LIST_FRAG_MAX];
    uint16_t    data_used;
    uint8_t     frag_count;

    uint8_t     count;
    uint8_t     selected;
    uint8_t     scroll_ofs;
    uint8_t     visible;

    uint8_t     x, y, w, h, item_h;
    KSCCOLOR    sel_bg, bg, fg, sel_fg;

    tile_h_t    tile;
    uint8_t     hw_opened;
    uint8_t     sel_style;

    app_t*          kpd;
    app_t*          tim;
    ctrl_keymap_t   km;
    ctrl_event_cb_t ctrl_cb;
    void*           ctrl_user;
    uint16_t        ctrl_interval_ms;
    uint8_t         ctrl_enabled;
} list_ctx_t;

/* ── 碎片管理 ── */

static void frag_add(list_ctx_t* ctx, uint8_t off, uint8_t sz)
{
    if ((uint16_t)off + sz == ctx->data_used) {
        ctx->data_used = off;
        return;
    }

    for (uint8_t i = 0; i < ctx->frag_count; i++) {
        list_frag_t* f = &ctx->frags[i];
        if (f->off + f->sz == off) {
            f->sz += sz;
            if (i + 1 < ctx->frag_count && off + sz == ctx->frags[i + 1].off) {
                f->sz += ctx->frags[i + 1].sz;
                ctx->frag_count--;
                memmove(&ctx->frags[i + 1], &ctx->frags[i + 2],
                        (ctx->frag_count - i - 1) * sizeof(list_frag_t));
            }
            return;
        }
        if (off + sz == f->off) {
            f->off = off;
            f->sz += sz;
            return;
        }
    }

    if (ctx->frag_count >= LIST_FRAG_MAX) return;

    uint8_t i;
    for (i = 0; i < ctx->frag_count; i++)
        if (off < ctx->frags[i].off) break;

    memmove(&ctx->frags[i + 1], &ctx->frags[i],
            (ctx->frag_count - i) * sizeof(list_frag_t));
    ctx->frags[i].off = off;
    ctx->frags[i].sz  = sz;
    ctx->frag_count++;
}

static void frag_compact(list_ctx_t* ctx)
{
    uint8_t dst = 0;
    for (uint8_t i = 0; i < ctx->count; i++) {
        uint8_t src = ctx->offsets[i];
        uint8_t sz  = (i == ctx->count - 1)
                      ? ctx->data_used - src
                      : ctx->offsets[i + 1] - src;
        if (src != dst)
            memmove(ctx->data_buf + dst, ctx->data_buf + src, sz);
        ctx->offsets[i] = dst;
        dst += sz;
    }
    ctx->data_used = dst;
    ctx->frag_count = 0;
}

/* ── 池操作 ── */

static int pool_add(list_ctx_t* ctx, const char* s)
{
    uint16_t len = (uint16_t)(strlen(s) + 1);
    if (ctx->count >= LIST_MAX_ITEMS || len > LIST_DATA_BUF)
        return -1;

    for (uint8_t i = 0; i < ctx->frag_count; i++) {
        list_frag_t* f = &ctx->frags[i];
        if (f->sz >= len) {
            memcpy(ctx->data_buf + f->off, s, len);
            ctx->offsets[ctx->count++] = f->off;
            uint8_t rem = f->sz - len;
            if (rem >= LIST_FRAG_MIN) {
                f->off += len;
                f->sz   = rem;
            } else {
                ctx->frag_count--;
                memmove(&ctx->frags[i], &ctx->frags[i + 1],
                        (ctx->frag_count - i) * sizeof(list_frag_t));
            }
            return 1;
        }
    }

    if (ctx->data_used + len <= LIST_DATA_BUF) {
        ctx->offsets[ctx->count++] = (uint8_t)ctx->data_used;
        memcpy(ctx->data_buf + ctx->data_used, s, len);
        ctx->data_used += len;
        return 1;
    }

    if (ctx->frag_count > 0) {
        frag_compact(ctx);
        if (ctx->data_used + len <= LIST_DATA_BUF) {
            ctx->offsets[ctx->count++] = (uint8_t)ctx->data_used;
            memcpy(ctx->data_buf + ctx->data_used, s, len);
            ctx->data_used += len;
            return 1;
        }
    }

    return -1;
}

static void pool_remove(list_ctx_t* ctx, uint8_t idx)
{
    if (idx >= ctx->count) return;

    uint8_t off = ctx->offsets[idx];
    uint8_t sz  = (idx == ctx->count - 1)
                  ? ctx->data_used - off
                  : ctx->offsets[idx + 1] - off;

    ctx->count--;
    memmove(&ctx->offsets[idx], &ctx->offsets[idx + 1],
            (ctx->count - idx) * sizeof(ctx->offsets[0]));

    if (ctx->data_used - off == sz)
        ctx->data_used = off;
    else
        frag_add(ctx, off, sz);

    if (ctx->count == 0) {
        ctx->selected = 0;
    } else {
        if (idx < ctx->selected)
            ctx->selected--;
        if (ctx->selected >= ctx->count)
            ctx->selected = ctx->count - 1;
    }
}

/* ── 渲染 ── */

static void obj_sync(list_ctx_t* ctx)
{
    ctx->visible = 0;
    if (ctx->count == 0) return;

    uint8_t max_fit = ctx->h / ctx->item_h;
    if (max_fit > LIST_MAX_VISIBLE) max_fit = LIST_MAX_VISIBLE;
    if (max_fit == 0) max_fit = 1;

    uint8_t n = ctx->count - ctx->scroll_ofs;
    if (n > max_fit) n = max_fit;
    ctx->visible = n;

    uint8_t pad = (ctx->sel_style == LIST_STYLE_ARROW) ? 12 : 4;

    for (uint8_t i = 0; i < n; i++) {
        uint8_t item = ctx->scroll_ofs + i;
        ksc_obj_t* o = &ctx->objs[i];
        o->sdx     = pad;
        o->sdy     = i * ctx->item_h + (ctx->item_h - Systemfont0.height) / 2;
        o->width   = ctx->w - pad - 4;
        o->height  = ctx->item_h;
        o->colorck = ctx->fg;
        o->data    = ctx->data_buf + ctx->offsets[item];
        o->_type   = _string;
    }
    for (uint8_t i = n; i < LIST_MAX_VISIBLE; i++)
        ctx->objs[i]._type = 0xFF;
}

static void list_render(list_ctx_t* ctx)
{
    if (ctx->visible == 0 || !ctx->gui) return;

    { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y 0 -w %d -h %d -c %04X", ctx->w, ctx->visible * ctx->item_h, (unsigned)ctx->bg); appcmd(ctx->gui, _b); }
    appwrite(ctx->gui, ctx->objs, LIST_MAX_VISIBLE, 0x02);

    uint8_t sel_i = ctx->selected - ctx->scroll_ofs;
    if (sel_i >= ctx->visible) return;

    uint8_t sy = sel_i * ctx->item_h;
    uint8_t ty = sy + (ctx->item_h - Systemfont0.height) / 2;

    switch (ctx->sel_style) {
    case LIST_STYLE_FILLROW:
        { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y %d -w %d -h %d -c %04X", sy, ctx->w, ctx->item_h, (unsigned)ctx->sel_bg); appcmd(ctx->gui, _b); }
        ctx->objs[sel_i].colorck = ctx->sel_fg;
        appwrite(ctx->gui, &ctx->objs[sel_i], 1, 0x01);
        break;
    case LIST_STYLE_FILLBAR:
        { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y %d -w 4 -h %d -c %04X", sy, ctx->item_h, (unsigned)ctx->sel_bg); appcmd(ctx->gui, _b); }
        break;
    case LIST_STYLE_ARROW:
        { char _b[72]; snprintf(_b, sizeof(_b), "char -x 2 -y %d -v 62 -c %04X -b %04X", ty, (unsigned)ctx->sel_fg, (unsigned)ctx->bg); appcmd(ctx->gui, _b); }
        break;
    case LIST_STYLE_TEXTONLY:
        ctx->objs[sel_i].colorck = ctx->sel_fg;
        appwrite(ctx->gui, &ctx->objs[sel_i], 1, 0x01);
        break;
    default:
        break;
    }
}

static void highlight_row(list_ctx_t* ctx, uint8_t item, KSCCOLOR bg)
{
    uint8_t i = item - ctx->scroll_ofs;
    if (i >= ctx->visible) return;

    uint8_t sy = i * ctx->item_h;
    uint8_t ty = sy + (ctx->item_h - Systemfont0.height) / 2;
    int is_sel = (bg == ctx->sel_bg);

    switch (ctx->sel_style) {
    case LIST_STYLE_FILLROW:
        { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y %d -w %d -h %d -c %04X", sy, ctx->w, ctx->item_h, (unsigned)bg); appcmd(ctx->gui, _b); }
        ctx->objs[i].colorck = is_sel ? ctx->sel_fg : ctx->fg;
        appwrite(ctx->gui, &ctx->objs[i], 1, 0x01);
        break;
    case LIST_STYLE_FILLBAR:
        { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y %d -w 4 -h %d -c %04X", sy, ctx->item_h, (unsigned)bg); appcmd(ctx->gui, _b); }
        break;
    case LIST_STYLE_ARROW:
        { char _b[72]; snprintf(_b, sizeof(_b), "char -x 2 -y %d -v 62 -c %04X -b %04X", ty, (unsigned)(is_sel ? ctx->sel_fg : ctx->bg), (unsigned)ctx->bg); appcmd(ctx->gui, _b); }
        break;
    case LIST_STYLE_TEXTONLY:
        { char _b[72]; snprintf(_b, sizeof(_b), "fill -x 0 -y %d -w %d -h %d -c %04X", sy, ctx->w, ctx->item_h, (unsigned)ctx->bg); appcmd(ctx->gui, _b); }
        ctx->objs[i].colorck = is_sel ? ctx->sel_fg : ctx->fg;
        appwrite(ctx->gui, &ctx->objs[i], 1, 0x01);
        break;
    default:
        break;
    }
}

/* ── 选中 ── */

static int do_select(list_ctx_t* ctx, uint8_t idx)
{
    if (ctx->count == 0 || idx >= ctx->count) return -1;

    uint8_t old   = ctx->selected;
    uint8_t oscr  = ctx->scroll_ofs;
    ctx->selected = idx;

    uint8_t max_fit = ctx->h / ctx->item_h;
    if (max_fit > LIST_MAX_VISIBLE) max_fit = LIST_MAX_VISIBLE;
    if (max_fit == 0) max_fit = 1;

    if (idx < ctx->scroll_ofs)
        ctx->scroll_ofs = idx;
    else if (idx >= ctx->scroll_ofs + max_fit)
        ctx->scroll_ofs = idx - max_fit + 1;

    if (ctx->hw_opened) {
        if (ctx->scroll_ofs != oscr) {
            obj_sync(ctx);
            list_render(ctx);
        } else if (idx != old) {
            highlight_row(ctx, old, ctx->bg);
            highlight_row(ctx, idx, ctx->sel_bg);
        }
    }

    if (ctx->app && ctx->app->user_func)
        ctx->app->user_func(ctx->app->input_data);

    return 0;
}

static int do_move(list_ctx_t* ctx, int delta)
{
    if (ctx->count == 0) return -1;
    int idx = (int)ctx->selected + delta;
    if (idx < 0) idx = 0;
    if (idx >= (int)ctx->count) idx = ctx->count - 1;
    if ((uint8_t)idx == ctx->selected) return 0;
    return do_select(ctx, (uint8_t)idx);
}

/* ── App 生命周期 ── */

static int list_open(app_t* app)
{
    list_ctx_t* ctx = (list_ctx_t*)osmalloc(sizeof(list_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(list_ctx_t));

    ctx->app     = app;
    ctx->gui     = app->app0;
    ctx->x = 0;  ctx->y = 0;
    ctx->w = 240; ctx->h = 160;
    ctx->item_h  = Systemfont0.height + 4;
    ctx->sel_bg  = 0x001F;
    ctx->bg      = 0x0000;
    ctx->fg      = 0xFFFF;
    ctx->sel_fg  = 0xFFFF;
    ctx->sel_style = LIST_STYLE_FILLROW;

    ctx->km.up = 6; ctx->km.down = 14; ctx->km.left = 9; ctx->km.right = 11; ctx->km.ok = 10; ctx->km.quit = 0;
    ctx->ctrl_interval_ms = CTRL_INTERVAL_MS;

    app->app_data = ctx;
    return 0;
}

static int list_close(app_t* app)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return 0;
    if (ctx->ctrl_enabled)
        appwrite(ctx->tim, NULL, 0, 0x42);
    if (ctx->hw_opened)
        appclose(ctx->gui);
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

/* ── appcmd handlers ── */

static void* list_tick_cb(void* data);

static int cmd_init(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (ctx->hw_opened) return -1;

    /* save ctrl flags before appcmd(ctx->gui, ...) corrupts argv */
    int want_k = APPCMD_HAS(argv, 'k');
    int has_u = want_k && APPCMD_HAS(argv, 'u');
    int has_d = want_k && APPCMD_HAS(argv, 'd');
    int has_l = want_k && APPCMD_HAS(argv, 'l');
    int has_r = want_k && APPCMD_HAS(argv, 'r');
    int has_o = want_k && APPCMD_HAS(argv, 'o');
    int has_q = want_k && APPCMD_HAS(argv, 'q');
    uint8_t k_up   = has_u ? (uint8_t)strtoul(argv[APPCMD_ARG('u')], NULL, 0) : 0;
    uint8_t k_down = has_d ? (uint8_t)strtoul(argv[APPCMD_ARG('d')], NULL, 0) : 0;
    uint8_t k_left = has_l ? (uint8_t)strtoul(argv[APPCMD_ARG('l')], NULL, 0) : 0;
    uint8_t k_right= has_r ? (uint8_t)strtoul(argv[APPCMD_ARG('r')], NULL, 0) : 0;
    uint8_t k_ok   = has_o ? (uint8_t)strtoul(argv[APPCMD_ARG('o')], NULL, 0) : 0;
    uint8_t k_quit = has_q ? (uint8_t)strtoul(argv[APPCMD_ARG('q')], NULL, 0) : 0;

    if (appopen(ctx->gui) < 0) return -1;
    appcmd(ctx->gui, "init");
    sysdelay(10);
    { char _b[80]; snprintf(_b, sizeof(_b), "wcreate -x %d -y %d -w %d -h %d -c %04X",
        ctx->x, ctx->y, ctx->w, ctx->h, ctx->bg);
      appcmd(ctx->gui, _b);
      ctx->tile = (tile_h_t)(uintptr_t)ctx->gui->output_data; }
    ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
    appcmd(ctx->gui, "wselect");
    ctx->hw_opened = 1;
    obj_sync(ctx);
    list_render(ctx);

    if (want_k) {
        if (!ctx->kpd) ctx->kpd = appget("button16");
        if (!ctx->tim) ctx->tim = appget("tim_clock");
        if (!ctx->kpd || !ctx->tim) return -1;
        if (has_u) ctx->km.up   = k_up;
        if (has_d) ctx->km.down = k_down;
        if (has_l) ctx->km.left = k_left;
        if (has_r) ctx->km.right= k_right;
        if (has_o) ctx->km.ok   = k_ok;
        if (has_q) ctx->km.quit = k_quit;
        ctx->ctrl_cb   = (ctrl_event_cb_t)app->mode_data;
        ctx->ctrl_user = app->output_data;
        if (appopen(ctx->kpd) < 0) return -1;
        appwrite(ctx->kpd, NULL, 0, 1);
        { uint32_t iv = ctx->ctrl_interval_ms; appwrite(ctx->kpd, &iv, 1, 2); }
        { uint32_t enable = 1; appwrite(ctx->kpd, &enable, 1, 4); }
        { uint32_t p[2] = {CTRL_HOLD_TICKS, CTRL_HOLD_GAP}; appwrite(ctx->kpd, p, 2, 5); }
        ctx->tim->user_func = list_tick_cb;
        ctx->tim->input_data = app;
        appopen(ctx->tim);
        appwrite(ctx->tim, NULL, ctx->ctrl_interval_ms, 0x41);
        appwrite(ctx->tim, NULL, 1, 0x42);
        ctx->ctrl_enabled = 1;
    }
    return 1;
}

static int cmd_add(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 'd')) return -1;
    const char* s = argv[APPCMD_ARG('d')];
    if (!s || !*s) return -1;
    int r = pool_add(ctx, s);
    if (ctx->hw_opened && r > 0) { obj_sync(ctx); list_render(ctx); }
    return r;
}

static int cmd_remove(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 'i')) return -1;
    int idx = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (idx < 0 || idx >= ctx->count) return -1;
    pool_remove(ctx, (uint8_t)idx);
    if (ctx->hw_opened && ctx->count > 0) { obj_sync(ctx); list_render(ctx); }
    return 1;
}

static int cmd_clear(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    ctx->count = 0; ctx->selected = 0; ctx->scroll_ofs = 0;
    ctx->data_used = 0; ctx->frag_count = 0;
    obj_sync(ctx);
    if (ctx->hw_opened) list_render(ctx);
    return 1;
}

static int cmd_compact(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    frag_compact(ctx);
    if (ctx->hw_opened) { obj_sync(ctx); list_render(ctx); }
    return 1;
}

static int cmd_select(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 'i')) return -1;
    int idx = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    return do_select(ctx, (uint8_t)idx);
}

static int cmd_confirm(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    if (ctx->app && ctx->app->user_func)
        ctx->app->user_func(ctx->app->input_data);
    return 1;
}

static int cmd_move(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 'n')) return -1;
    int delta = (int)strtol(argv[APPCMD_ARG('n')], NULL, 0);
    return do_move(ctx, delta);
}

static int cmd_getcount(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    return (int)ctx->count;
}

static int cmd_getlabel(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 'i')) return -1;
    int idx = (int)strtoul(argv[APPCMD_ARG('i')], NULL, 0);
    if (idx < 0 || idx >= ctx->count) return -1;
    const char* s = ctx->data_buf + ctx->offsets[idx];
    int len = (int)strlen(s) + 1;
    if (app->input_data) memcpy(app->input_data, s, (size_t)len);
    return len;
}

static int cmd_setpos(app_t* app, list_ctx_t* ctx, const char** argv)
{
    if (APPCMD_HAS(argv, 'x')) ctx->x = (uint8_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
    else if (app->input_data) { const list_pos_t* p = (const list_pos_t*)app->input_data; ctx->x = p->x; ctx->y = p->y; ctx->w = p->w; ctx->h = p->h; ctx->item_h = p->item_h; }
    if (APPCMD_HAS(argv, 'y')) ctx->y = (uint8_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0);
    if (APPCMD_HAS(argv, 'w')) ctx->w = (uint8_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0);
    if (APPCMD_HAS(argv, 'h')) ctx->h = (uint8_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    if (APPCMD_HAS(argv, 't')) ctx->item_h = (uint8_t)strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (ctx->hw_opened) {
        ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
        { char _b[64]; snprintf(_b, sizeof(_b), "wmove -x %d -y %d", ctx->x, ctx->y);
          appcmd(ctx->gui, _b); }
        { char _b[64]; snprintf(_b, sizeof(_b), "wresize -w %d -h %d", ctx->w, ctx->h);
          appcmd(ctx->gui, _b); }
    }
    return 1;
}

static int cmd_setcolors(app_t* app, list_ctx_t* ctx, const char** argv)
{
    if (APPCMD_HAS(argv, 'a')) ctx->sel_bg = (KSCCOLOR)strtoul(argv[APPCMD_ARG('a')], NULL, 16);
    else if (app->input_data) { const list_colors_t* c = (const list_colors_t*)app->input_data; ctx->sel_bg = c->sel_bg; ctx->bg = c->bg; ctx->fg = c->fg; ctx->sel_fg = c->sel_fg; }
    if (APPCMD_HAS(argv, 'b')) ctx->bg     = (KSCCOLOR)strtoul(argv[APPCMD_ARG('b')], NULL, 16);
    if (APPCMD_HAS(argv, 'c')) ctx->fg     = (KSCCOLOR)strtoul(argv[APPCMD_ARG('c')], NULL, 16);
    if (APPCMD_HAS(argv, 'd')) ctx->sel_fg = (KSCCOLOR)strtoul(argv[APPCMD_ARG('d')], NULL, 16);
    if (ctx->hw_opened) {
        ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
        { char _b[48]; snprintf(_b, sizeof(_b), "wbk -c %04X", ctx->bg);
          appcmd(ctx->gui, _b); }
        obj_sync(ctx);
        list_render(ctx);
    }
    return 1;
}

static int cmd_setstyle(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (!APPCMD_HAS(argv, 's')) return -1;
    int style = (int)strtoul(argv[APPCMD_ARG('s')], NULL, 0);
    if (style < LIST_STYLE_NONE || style > LIST_STYLE_ARROW) return -1;
    ctx->sel_style = (uint8_t)style;
    if (ctx->hw_opened) { obj_sync(ctx); list_render(ctx); }
    return 1;
}

static int cmd_refresh(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    obj_sync(ctx);
    list_render(ctx);
    return 1;
}

/* ── 键盘控制定时回调 ── */

static void* list_tick_cb(void* data)
{
    app_t* app = (app_t*)data;
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx || !ctx->ctrl_enabled) return NULL;

    uint32_t ev;
    while (ctx->kpd && appread(ctx->kpd, &ev, 0, 3) > 0) {
        uint8_t key  = (ev >> 4) & 0xF;
        uint8_t type = ev & 0xF;

        if (key == ctx->km.up || key == ctx->km.down || key == ctx->km.left || key == ctx->km.right) {
            if (type == 0 || type == 2 || type == 4) {
                int delta = (key == ctx->km.down || key == ctx->km.right) ? 1 : -1;
                do_move(ctx, delta);
            }
        } else if (type == 0) {
            if (key == ctx->km.ok) {
                cmd_confirm(app, ctx, NULL);
                if (ctx->ctrl_cb)
                    ctx->ctrl_cb(ctx->ctrl_user, CTRL_EVENT_CONFIRM);
            } else if (key == ctx->km.quit) {
                if (ctx->ctrl_cb)
                    ctx->ctrl_cb(ctx->ctrl_user, CTRL_EVENT_QUIT);
            }
        }
    }
    return NULL;
}

/* ── appcmd dispatch ── */

typedef int (*list_cmd_handler_t)(app_t*, list_ctx_t*, const char**);

typedef struct {
    const char*          name;
    list_cmd_handler_t   handler;
} list_cmd_entry_t;

static const list_cmd_entry_t cmd_table_new[] = {
    {"init",      cmd_init},
    {"add",       cmd_add},
    {"remove",    cmd_remove},
    {"clear",     cmd_clear},
    {"compact",   cmd_compact},
    {"select",    cmd_select},
    {"confirm",   cmd_confirm},
    {"move",      cmd_move},
    {"getcount",  cmd_getcount},
    {"getlabel",  cmd_getlabel},
    {"setpos",    cmd_setpos},
    {"setcolors", cmd_setcolors},
    {"setstyle",  cmd_setstyle},
    {"refresh",   cmd_refresh},
};

static int list_cmd(app_t* app, const char* cmdname, const char** argv)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return -1;

    for (size_t i = 0; i < sizeof(cmd_table_new) / sizeof(cmd_table_new[0]); i++) {
        if (strcmp(cmdname, cmd_table_new[i].name) == 0)
            return cmd_table_new[i].handler(app, ctx, argv);
    }
    return -1;
}

/* ── read ── */

static int list_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)count;
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx || !data) return 0;

    if (mode == 1) {
        *(uint32_t*)data = ctx->selected;
        return 4;
    }

    if (mode == 2) {
        int idx = (int)count;
        if (idx >= 0 && idx < ctx->count) {
            const char* src = ctx->data_buf + ctx->offsets[idx];
            uint8_t len = strlen(src) + 1;
            memcpy(data, src, len);
            return (int)len;
        }
    }

    return 0;
}

/* ── write (no-op) ── */

static int list_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

/* ── 注册 ── */

static const papp_ops_t list_ops = {
    .open  = list_open,
    .close = list_close,
    .read  = list_read,
    .write = list_write,
    .cmd   = list_cmd,
};

REGISTER_APP_EX("list", NULL, "1\0KSCGUI",
                &list_ops, "GUI list widget (256B pool, frag mgmt, user_func)");

#endif
