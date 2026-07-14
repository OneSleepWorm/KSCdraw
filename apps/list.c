/**
 * @file    list.c
 * @note    GUI List Widget — 字符串池 + 碎片管理 + 多选中样式 + 回调
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
 * 全部数据操作走 appioctl，无 appwrite:
 *
 *   appget("list") -> app_t*
 *   appopen(list)  -> 分配 context, 设默认值
 *   appclose(list) -> 释放
 *
 *   --- 数据操作 ---
 *   appioctl(list, "add", "label")      -> 追加一项
 *   appioctl(list, "remove", idx)       -> 删除一项
 *   appioctl(list, "clear")             -> 清空
 *   appioctl(list, "compact")           -> 手动碎片整理
 *
 *   --- 选中 ---
 *   appioctl(list, "select", idx)       -> 选中指定项
 *   appioctl(list, "move", delta)       -> [+1/-1] 移动选中
 *   appioctl(list, "confirm")           -> 触发 callback
 *   选中变化时自动调用 app->callback(app->user_data)
 *
 *   --- 查询 ---
 *   appioctl(list, "getcount")          -> int 总项数
 *   appioctl(list, "getlabel", idx, &p) -> const char* 零拷贝指针
 *   appread(list, &sel, 0, 1)          -> 读 selected
 *   appread(list, buf, idx, 2)         -> 拷贝标签
 *
 *   --- 外观 ---
 *   appioctl(list, "setpos", &pos)      -> list_pos_t 结构体
 *   appioctl(list, "setcolors", &c)     -> list_colors_t 结构体
 *   appioctl(list, "setstyle", s)       -> 选中样式 (0-4)
 *   appioctl(list, "init")              -> 创建 KSCGUI 窗口 + 首绘
 *   appioctl(list, "refresh")           -> 全量重绘
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
 *   1. GUI_FILL 清空可视区域 (防字符串残留)
 *   2. GUI_DRAWOBJS 画所有文字
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
 *   list->callback = my_cb;
 *   list->user_data = my_ctx;
 *   appopen(list);
 *
 *   appioctl(list, "add", "System");
 *   appioctl(list, "add", "Settings");
 *   appioctl(list, "add", "Media");
 *
 *   list_pos_t pos = {0, 0, 240, 320, 24};
 *   list_colors_t col = {0x001F, 0x0000, 0xFFFF, 0xF800};
 *   appioctl(list, "setpos", &pos);
 *   appioctl(list, "setcolors", &col);
 *   appioctl(list, "setstyle", LIST_STYLE_FILLROW);
 *   appioctl(list, "init");
 *
 *   appioctl(list, "move", 1);
 *   uint32_t sel;
 *   appread(list, &sel, 0, 1);
 *
 *   appclose(list);
 *
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/kscgui.h"
#include "../inc/KSCOSsystem.h"
#include "../inc/KSCfont.h"
#include "../inc/list.h"
#include <string.h>
#include <stdlib.h>

#if __USE_STM32__

/* ── 常量 ── */

#define LIST_DATA_BUF     256
#define LIST_MAX_ITEMS    32
#define LIST_MAX_VISIBLE  16
#define LIST_ITEM_H_DEF   20
#define LIST_FRAG_MAX     8
#define LIST_FRAG_MIN     8

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
    uint8_t len = strlen(s) + 1;
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

    GUI_FILL(ctx->gui, 0, 0, ctx->w, ctx->visible * ctx->item_h, ctx->bg);
    GUI_DRAWOBJS(ctx->gui, ctx->objs, LIST_MAX_VISIBLE);

    uint8_t sel_i = ctx->selected - ctx->scroll_ofs;
    if (sel_i >= ctx->visible) return;

    uint8_t sy = sel_i * ctx->item_h;
    uint8_t ty = sy + (ctx->item_h - Systemfont0.height) / 2;

    switch (ctx->sel_style) {
    case LIST_STYLE_FILLROW:
        GUI_FILL(ctx->gui, 0, sy, ctx->w, ctx->item_h, ctx->sel_bg);
        ctx->objs[sel_i].colorck = ctx->sel_fg;
        GUI_DRAWOBJ(ctx->gui, &ctx->objs[sel_i]);
        break;
    case LIST_STYLE_FILLBAR:
        GUI_FILL(ctx->gui, 0, sy, 4, ctx->item_h, ctx->sel_bg);
        break;
    case LIST_STYLE_ARROW:
        GUI_CHAR(ctx->gui, 2, ty, '>', ctx->sel_fg, ctx->bg);
        break;
    case LIST_STYLE_TEXTONLY:
        ctx->objs[sel_i].colorck = ctx->sel_fg;
        GUI_DRAWOBJ(ctx->gui, &ctx->objs[sel_i]);
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
        GUI_FILL(ctx->gui, 0, sy, ctx->w, ctx->item_h, bg);
        ctx->objs[i].colorck = is_sel ? ctx->sel_fg : ctx->fg;
        GUI_DRAWOBJ(ctx->gui, &ctx->objs[i]);
        break;
    case LIST_STYLE_FILLBAR:
        GUI_FILL(ctx->gui, 0, sy, 4, ctx->item_h, bg);
        break;
    case LIST_STYLE_ARROW:
        GUI_CHAR(ctx->gui, 2, ty, '>',
                 is_sel ? ctx->sel_fg : ctx->bg, ctx->bg);
        break;
    case LIST_STYLE_TEXTONLY:
        GUI_FILL(ctx->gui, 0, sy, ctx->w, ctx->item_h, ctx->bg);
        ctx->objs[i].colorck = is_sel ? ctx->sel_fg : ctx->fg;
        GUI_DRAWOBJ(ctx->gui, &ctx->objs[i]);
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

    if (ctx->app && ctx->app->callback)
        ctx->app->callback(ctx->app->user_data);

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

    app->app_data = ctx;
    return 0;
}

static int list_close(app_t* app)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return 0;
    if (ctx->hw_opened)
        appclose(ctx->gui);
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

/* ── Handler ── */

static int handler_init(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    if (ctx->hw_opened) return -1;
    if (appopen(ctx->gui) < 0) return -1;

    appcmd(ctx->gui, "init");
    sysdelay(10);
    { char _b[80]; snprintf(_b, sizeof(_b), "wcreate -x %d -y %d -w %d -h %d -c %04X",
        ctx->x, ctx->y, ctx->w, ctx->h, ctx->bg);
      appcmd(ctx->gui, _b);
      ctx->tile = (tile_h_t)(uintptr_t)ctx->gui->callback_data; }
    ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
    appcmd(ctx->gui, "wselect");
    ctx->hw_opened = 1;

    obj_sync(ctx);
    list_render(ctx);
    return 1;
}

static int handler_add(list_ctx_t* ctx, va_list ap)
{
    const char* s = va_arg(ap, const char*);
    int r = pool_add(ctx, s);
    if (ctx->hw_opened && r > 0) {
        obj_sync(ctx);
        list_render(ctx);
    }
    return r;
}

static int handler_remove(list_ctx_t* ctx, va_list ap)
{
    int idx = va_arg(ap, int);
    if (idx < 0 || idx >= ctx->count) return -1;
    pool_remove(ctx, (uint8_t)idx);
    if (ctx->hw_opened && ctx->count > 0) {
        obj_sync(ctx);
        list_render(ctx);
    }
    return 1;
}

static int handler_clear(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    ctx->count      = 0;
    ctx->selected   = 0;
    ctx->scroll_ofs = 0;
    ctx->data_used  = 0;
    ctx->frag_count = 0;
    obj_sync(ctx);
    if (ctx->hw_opened) list_render(ctx);
    return 1;
}

static int handler_compact(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    frag_compact(ctx);
    if (ctx->hw_opened) {
        obj_sync(ctx);
        list_render(ctx);
    }
    return 1;
}

static int handler_select(list_ctx_t* ctx, va_list ap)
{
    int idx = va_arg(ap, int);
    return do_select(ctx, (uint8_t)idx);
}

static int handler_confirm(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    if (ctx->app && ctx->app->callback)
        ctx->app->callback(ctx->app->user_data);
    return 1;
}

static int handler_move(list_ctx_t* ctx, va_list ap)
{
    int delta = va_arg(ap, int);
    return do_move(ctx, delta);
}

static int handler_getcount(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    return (int)ctx->count;
}

static int handler_getlabel(list_ctx_t* ctx, va_list ap)
{
    int idx = va_arg(ap, int);
    const char** out = va_arg(ap, const char**);
    if (idx < 0 || idx >= ctx->count) return -1;
    *out = ctx->data_buf + ctx->offsets[idx];
    return 1;
}

static int handler_setpos(list_ctx_t* ctx, va_list ap)
{
    const list_pos_t* p = va_arg(ap, const list_pos_t*);
    ctx->x = p->x; ctx->y = p->y;
    ctx->w = p->w; ctx->h = p->h;
    ctx->item_h = p->item_h ? p->item_h : (uint8_t)(Systemfont0.height + 4);
    if (ctx->hw_opened) {
        ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
        { char _b[64]; snprintf(_b, sizeof(_b), "wmove -x %d -y %d", ctx->x, ctx->y);
          appcmd(ctx->gui, _b); }
        { char _b[64]; snprintf(_b, sizeof(_b), "wresize -w %d -h %d", ctx->w, ctx->h);
          appcmd(ctx->gui, _b); }
    }
    return 1;
}

static int handler_setcolors(list_ctx_t* ctx, va_list ap)
{
    const list_colors_t* c = va_arg(ap, const list_colors_t*);
    ctx->sel_bg = c->sel_bg;
    ctx->bg     = c->bg;
    ctx->fg     = c->fg;
    ctx->sel_fg = c->sel_fg;
    if (ctx->hw_opened) {
        ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
        { char _b[48]; snprintf(_b, sizeof(_b), "wbk -c %04X", ctx->bg);
          appcmd(ctx->gui, _b); }
        obj_sync(ctx);
        list_render(ctx);
    }
    return 1;
}

static int handler_refresh(list_ctx_t* ctx, va_list ap)
{
    (void)ap;
    obj_sync(ctx);
    list_render(ctx);
    return 1;
}

static int handler_setstyle(list_ctx_t* ctx, va_list ap)
{
    int style = va_arg(ap, int);
    if (style < LIST_STYLE_NONE || style > LIST_STYLE_ARROW)
        return -1;
    ctx->sel_style = (uint8_t)style;
    if (ctx->hw_opened) {
        obj_sync(ctx);
        list_render(ctx);
    }
    return 1;
}

/* ── appcmd handlers ── */

static int cmd_init(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app; (void)argv;
    if (ctx->hw_opened) return -1;
    if (appopen(ctx->gui) < 0) return -1;
    appcmd(ctx->gui, "init");
    sysdelay(10);
    { char _b[80]; snprintf(_b, sizeof(_b), "wcreate -x %d -y %d -w %d -h %d -c %04X",
        ctx->x, ctx->y, ctx->w, ctx->h, ctx->bg);
      appcmd(ctx->gui, _b);
      ctx->tile = (tile_h_t)(uintptr_t)ctx->gui->callback_data; }
    ctx->gui->mode_data = (void*)(uintptr_t)ctx->tile;
    appcmd(ctx->gui, "wselect");
    ctx->hw_opened = 1;
    obj_sync(ctx);
    list_render(ctx);
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
    if (ctx->app && ctx->app->callback)
        ctx->app->callback(ctx->app->user_data);
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
    if (app->user_data) memcpy(app->user_data, s, (size_t)len);
    return len;
}

static int cmd_setpos(app_t* app, list_ctx_t* ctx, const char** argv)
{
    (void)app;
    if (APPCMD_HAS(argv, 'x')) ctx->x = (uint8_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0);
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
    (void)app;
    if (APPCMD_HAS(argv, 'a')) ctx->sel_bg = (KSCCOLOR)strtoul(argv[APPCMD_ARG('a')], NULL, 16);
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

/* ── 命令表 ── */

typedef int (*list_handler_t)(list_ctx_t*, va_list);

typedef struct {
    const char*    name;
    list_handler_t handler;
} list_cmd_t;

static const list_cmd_t cmd_table[] = {
    {"init",      handler_init},
    {"add",       handler_add},
    {"remove",    handler_remove},
    {"clear",     handler_clear},
    {"compact",   handler_compact},
    {"select",    handler_select},
    {"confirm",   handler_confirm},
    {"move",      handler_move},
    {"getcount",  handler_getcount},
    {"getlabel",  handler_getlabel},
    {"setpos",    handler_setpos},
    {"setcolors", handler_setcolors},
    {"setstyle",  handler_setstyle},
    {"refresh",   handler_refresh},
};

/* ── ioctl ── */

static int list_ioctl(app_t* app, const char* fmt, va_list ap)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return -1;

    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); i++) {
        if (strcmp(fmt, cmd_table[i].name) == 0)
            return cmd_table[i].handler(ctx, ap);
    }

    return 0;
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
                &list_ops, "GUI list widget (256B pool, frag mgmt, callback)");

#endif
