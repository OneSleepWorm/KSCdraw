/**
 * @file    list.c
 * @note    GUI List Widget — Nokia 风格单列列表 (obj 虚拟渲染)
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
 * ROM(Debug -O0):  2144 B
 * ROM(Release -Os): 1056 B
 * RAM(静态): 0 B
 * RAM(堆):  约 720 B (list_ctx_t: 16 objs × 12 + 32 labels × 16 + 字段)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * appget("list") → app_t*
 * appopen(list) → 分配 context, 设默认值
 * appwrite(list, data, count, mode) → 设列表项
 * appread(list, data, count, mode) → 读状态
 * appioctl(list, "init") → 创建窗口 + 首绘
 * appioctl(list, "config", ...) → 自定义外观
 * appioctl(list, "select", idx) → 选中某项
 * appioctl(list, "move", delta) → 上/下移动
 * appioctl(list, "refresh") → 全量重绘
 * appclose(list) → 释放
 *
 * 典型使用示例:
 *
 *   app_t* list = appget("list");
 *   appopen(list);
 *   appwrite(list, "A\0B\0C\0D\0", 4, 1);
 *   appioctl(list, "config", 0,0,240,320,0, 0x001F, 0x0000, 0xFFFF, 0xFFFF);
 *   appioctl(list, "init");
 *   appioctl(list, "move", 1);
 *   uint32_t sel;
 *   appread(list, &sel, 0, 1);
 *   appclose(list);
 *
 * ============================================================
 * appwrite / appread mode 表
 * ============================================================
 * 操作  | mode | data        | count  | 功能
 * ------+------+-------------+--------+-----------------------------
 * write |  1   | const char* | 项数   | 打包\0分隔字符串设列表项
 * read  |  1   | uint32_t*   | 0      | 返回 selected index
 * read  |  2   | char*       | index  | 拷贝标签到 data (max 16B)
 *
 * ============================================================
 * appioctl 命令表
 * ============================================================
 * "config"  → int x,y,w,h,item_h, sel_c,bg_c,fg_c,sel_fg_c  (9参数全填)
 * "init"    → 创建窗口 + 首绘
 * "select"  → int idx
 * "move"    → int delta (+1=下, -1=上)
 * "refresh" → 全量重绘
 *
 * ============================================================
 * 输入绑定策略
 * ============================================================
 * list 不持有任何输入驱动 (button16/touch).
 * 上层 app 读取输入后调用 list_move/list_select.
 * 预留将来通过 callback 或 "bind" ioctl 注册回调.
 * ============================================================
 */

#include "../inc/app.h"
#include "../inc/kscgui.h"
#include "../inc/KSCOSsystem.h"
#include "../inc/KSCfont.h"
#include <string.h>

#if __USE_STM32__

/* ── 常量 ── */

#define LIST_MAX_VISIBLE  16
#define MAX_OBJS          LIST_MAX_VISIBLE
#define LIST_MAX_ITEMS    32
#define LIST_LABEL_MAX    16
#define LIST_ITEM_H_DEF   20

/* ── Context ── */

typedef struct {
    app_t*     gui;
    ksc_obj_t  objs[MAX_OBJS];
    char       labels[LIST_MAX_ITEMS][LIST_LABEL_MAX];

    uint8_t    count;
    uint8_t    selected;
    uint8_t    scroll_ofs;
    uint8_t    visible;
    uint8_t    x, y, w;
    uint16_t   h;
    uint8_t    item_h;
    KSCCOLOR   sel_c, bg_c, fg_c, sel_fg_c;
    uint8_t    hw_opened;
} list_ctx_t;

/* ── 内部渲染 ── */

static void list_sync_objs(list_ctx_t* ctx)
{
    uint8_t i;
    for (i = 0; i < MAX_OBJS; i++)
        ctx->objs[i]._type &= ~(_active | _visible | _dirty);

    if (ctx->count == 0) {
        ctx->visible = 0;
        return;
    }

    uint8_t scroll_ofs = ctx->scroll_ofs;
    uint8_t avail_px = (ctx->h > 255) ? 255 : (uint8_t)ctx->h;
    uint8_t max_fit = avail_px / ctx->item_h;
    uint8_t visible = ctx->count - scroll_ofs;
    if (visible > max_fit) visible = max_fit;
    if (visible > LIST_MAX_VISIBLE) visible = LIST_MAX_VISIBLE;
    ctx->visible = visible;

    for (i = 0; i < visible; i++) {
        uint8_t item  = scroll_ofs + i;
        uint8_t sy    = i * ctx->item_h;

        ksc_obj_t* tx = &ctx->objs[i];
        tx->sdx     = ctx->x + 4;
        tx->sdy     = sy + (ctx->item_h - Systemfont0.height) / 2;
        tx->width   = ctx->w - 8;
        tx->height  = ctx->item_h;
        tx->colorck = (item == ctx->selected) ? ctx->sel_fg_c : ctx->fg_c;
        tx->data    = ctx->labels[item];
        tx->_type   = _string | _active | _visible | _dirty;
    }
}

static void list_draw(list_ctx_t* ctx)
{
    if (ctx->visible == 0 || !ctx->gui) return;
    appioctl(ctx->gui, "setobjs", (int)(ctx->visible), ctx->objs);
    appioctl(ctx->gui, "drawobjs", (int)(ctx->visible));
}

static void list_draw_row(list_ctx_t* ctx, uint8_t item)
{
    uint8_t i = item - ctx->scroll_ofs;
    if (i >= ctx->visible) return;
    appioctl(ctx->gui, "drawobj", (int)i);
}

static void list_set_row_sel(list_ctx_t* ctx, uint8_t item, uint8_t selected)
{
    uint8_t i = item - ctx->scroll_ofs;
    if (i >= ctx->visible) return;
    ctx->objs[i].colorck = selected ? ctx->sel_fg_c : ctx->fg_c;
    ctx->objs[i]._type  |= _dirty;
}

static int list_select(list_ctx_t* ctx, uint8_t idx)
{
    if (ctx->count == 0 || idx >= ctx->count) return -1;
    uint8_t old_sel     = ctx->selected;
    uint8_t old_scroll  = ctx->scroll_ofs;
    ctx->selected       = idx;

    uint8_t avail_px = (ctx->h > 255) ? 255 : (uint8_t)ctx->h;
    uint8_t max_fit = avail_px / ctx->item_h;
    if (max_fit > LIST_MAX_VISIBLE) max_fit = LIST_MAX_VISIBLE;
    if (max_fit == 0) max_fit = 1;

    if (idx < ctx->scroll_ofs)
        ctx->scroll_ofs = idx;
    else if (idx >= ctx->scroll_ofs + max_fit)
        ctx->scroll_ofs = idx - max_fit + 1;
    else if (ctx->scroll_ofs + max_fit > ctx->count && ctx->count >= max_fit)
        ctx->scroll_ofs = ctx->count - max_fit;

    if (!ctx->hw_opened) return 0;

    if (ctx->scroll_ofs != old_scroll) {
        /* 滚动变化 → 全量重绘 */
        list_sync_objs(ctx);
        list_draw(ctx);
    } else if (idx != old_sel) {
        /* 仅选中变化 → 增量: 更新 2 行颜色 */
        list_set_row_sel(ctx, old_sel, 0);
        list_set_row_sel(ctx, idx,    1);
        list_draw_row(ctx, old_sel);
        list_draw_row(ctx, idx);
    }
    return 0;
}

static int list_move(list_ctx_t* ctx, int delta)
{
    if (ctx->count == 0) return -1;
    int idx = (int)ctx->selected + delta;
    if (idx < 0) idx = 0;
    if (idx >= (int)ctx->count) idx = (int)ctx->count - 1;
    if ((uint8_t)idx == ctx->selected) return 0;
    return list_select(ctx, (uint8_t)idx);
}

/* ── App 生命周期 ── */

static int list_open(app_t* app)
{
    list_ctx_t* ctx = (list_ctx_t*)osmalloc(sizeof(list_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(list_ctx_t));
    ctx->gui      = app->app0;
    ctx->x        = 0;
    ctx->y        = 0;
    ctx->w        = 240;
    ctx->h        = 300;
    ctx->item_h   = Systemfont0.height + 4;
    ctx->sel_c    = 0x001F;
    ctx->bg_c     = 0x0000;
    ctx->fg_c     = 0xFFFF;
    ctx->sel_fg_c = 0xFFFF;
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

static int list_do_init(app_t* app)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (ctx->hw_opened) return -1;

    if (appopen(ctx->gui) < 0) return -1;
    appioctl(ctx->gui, "init");
    sysdelay(10);
    int win = appioctl(ctx->gui, "wcreate", 0, ctx->y, ctx->w,
                       ctx->h, 0x0000);
    appioctl(ctx->gui, "wselect", win);
    ctx->hw_opened = 1;

    list_sync_objs(ctx);
    list_draw(ctx);
    return 0;
}

/* ── ioctl ── */

static int list_ioctl(app_t* app, const char* fmt, va_list ap)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return -1;

    if (strcmp(fmt, "init") == 0)
        return list_do_init(app);

    if (strcmp(fmt, "config") == 0) {
        ctx->x        = (uint8_t)va_arg(ap, int);
        ctx->y        = (uint8_t)va_arg(ap, int);
        ctx->w        = (uint8_t)va_arg(ap, int);
        ctx->h        = (uint16_t)va_arg(ap, int);
        ctx->item_h   = (uint8_t)va_arg(ap, int);
        if (ctx->item_h == 0)
            ctx->item_h = Systemfont0.height + 4;
        ctx->sel_c    = (KSCCOLOR)va_arg(ap, int);
        ctx->bg_c     = (KSCCOLOR)va_arg(ap, int);
        ctx->fg_c     = (KSCCOLOR)va_arg(ap, int);
        ctx->sel_fg_c = (KSCCOLOR)va_arg(ap, int);
        return 0;
    }

    if (strcmp(fmt, "select") == 0) {
        int idx = va_arg(ap, int);
        return list_select(ctx, (uint8_t)idx);
    }

    if (strcmp(fmt, "move") == 0) {
        int delta = va_arg(ap, int);
        return list_move(ctx, delta);
    }

    if (strcmp(fmt, "refresh") == 0) {
        list_sync_objs(ctx);
        list_draw(ctx);
        return 0;
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

    if (mode == 2 && count < ctx->count) {
        strncpy((char*)data, ctx->labels[count], LIST_LABEL_MAX);
        return LIST_LABEL_MAX;
    }

    return 0;
}

/* ── write ── */

static int list_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    list_ctx_t* ctx = (list_ctx_t*)app->app_data;
    if (!ctx) return 0;

    if (mode == 1 && data && count > 0) {
        const char* p = (const char*)data;
        ctx->count = 0;
        uint32_t i;
        for (i = 0; i < count && ctx->count < LIST_MAX_ITEMS; i++) {
            strncpy(ctx->labels[ctx->count], p, LIST_LABEL_MAX - 1);
            ctx->labels[ctx->count][LIST_LABEL_MAX - 1] = '\0';
            ctx->count++;
            p += strlen(p) + 1;
        }
        ctx->selected   = 0;
        ctx->scroll_ofs = 0;
        return (int)ctx->count;
    }

    return 0;
}

static const papp_ops_t list_ops = {
    .open   = list_open,
    .close  = list_close,
    .read   = list_read,
    .write  = list_write,
    .ioctl  = list_ioctl,
};

/* app_dep: KSCGUI(app0) */
REGISTER_APP_EX("list", NULL, "1\0KSCGUI",
                &list_ops, "GUI list widget (Nokia-style, obj-based)");

#endif
