#ifndef __KSCGUI_H__
#define __KSCGUI_H__

#include <stdio.h>      /* snprintf */
#include "app.h"
#include "KSCdraw.h"   /* ksc_obj_t 类型 */

/* ================================================================
 * tile 句柄类型
 *
 * tile_h_t 为不透明句柄，0 = 无效。
 * 内部编码: [4-bit generation | 4-bit slot index]
 * ================================================================ */
typedef uint8_t tile_h_t;

/* GUI_WINFO 返回的 tile 信息 */
typedef struct {
    tile_h_t  handle;
    uint16_t  x, y, w, h;
    KSCCOLOR  bk;
    uint8_t   visible;
    uint8_t   z;
    uint8_t   is_active;
    uint16_t  obj_count;
} tile_info_t;

/* ================================================================
 * 宏定义 API
 *
 * 用法:
 *   app_t* gui = appget("KSCGUI");
 *   appopen(gui);
 *   GUI_SETSPI(gui, 2);     // 选 SPI2
 *   GUI_INIT(gui);          // 初始化 ST7789
 *   GUI_CLEAR(gui, 0x0000); // 清屏为黑
 *   ...
 * ================================================================ */

/* ================================================================
 * 生命周期
 * ================================================================ */

/* 初始化 ST7789 屏幕（发 init 序列）
 * g: app_t* 句柄 */
#define GUI_INIT(g)              appioctl(g, "init")

/* 选 SPI 实例 (1 或 2)
 * g: app_t* 句柄, n: 1=SPI1, 2=SPI2 */
#define GUI_SETSPI(g,n)          appioctl(g, "setspi", (int)(n))

/* 设置屏幕方向 (写 0x36 寄存器)
 * g: app_t* 句柄, m: MADCTL 值 */
#define GUI_ORIENT(g,m)          appioctl(g, "orient", (int)(m))

/* ================================================================
 * tile 管理
 * ================================================================ */

/* 创建 tile，返回 tile_h_t（0=失败）
 * g: app_t* 句柄, x,y: 屏幕坐标, w,h: 宽高, b: 背景色 (KSCCOLOR) */
#define GUI_WCREATE(g,x,y,w,h,b) ((tile_h_t)(uint8_t)appioctl(g,"wcreate",(int)(x),(int)(y),(int)(w),(int)(h),(int)(b)))

/* 删除 tile
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_WDELETE(g,h)         appioctl(g, "wdelete", (int)(h))

/* 设 active tile（后续绘图命令的目标）
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_WSELECT(g,h)         appioctl(g, "wselect", (int)(h))

/* 用 tile 背景色填充 active tile
 * g: app_t* 句柄 */
#define GUI_WCLEAR(g)            appioctl(g, "wclear")

/* ================================================================
 * tile 可见性
 * ================================================================ */

/* 隐藏 tile（数据保留，不渲染）
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_WHIDE(g,h)           appioctl(g, "whide", (int)(h))

/* 显示 tile
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_WSHEW(g,h)           appioctl(g, "wshew", (int)(h))

/* 切换 tile 可见/隐藏
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_WTOGGLE(g,h)         appioctl(g, "wtoggle", (int)(h))

/* ================================================================
 * tile 属性
 * ================================================================ */

/* 移动 tile（改 ssx/ssy）
 * g: app_t* 句柄, h: tile_h_t 句柄, x,y: 新坐标 */
#define GUI_WMOVE(g,h,x,y)       appioctl(g, "wmove", (int)(h),(int)(x),(int)(y))

/* 改变 tile 大小
 * g: app_t* 句柄, h: tile_h_t 句柄, w,hgt: 新宽高 */
#define GUI_WRESIZE(g,h,w,hgt)   appioctl(g, "wresize", (int)(h),(int)(w),(int)(hgt))

/* 改 tile 背景色
 * g: app_t* 句柄, h: tile_h_t 句柄, c: KSCCOLOR */
#define GUI_WBK(g,h,c)           appioctl(g, "wbk", (int)(h),(int)(c))

/* 设 Z 序（值越大越靠上）
 * g: app_t* 句柄, h: tile_h_t 句柄, z: 0-255 */
#define GUI_WZORDER(g,h,z)       appioctl(g, "wzorder", (int)(h),(int)(z))

/* ================================================================
 * tile 查询
 * ================================================================ */

/* 返回 active tile 句柄
 * g: app_t* 句柄 */
#define GUI_WACTIVE(g)           ((tile_h_t)(uint8_t)appioctl(g, "wactive"))

/* 读 tile 信息到 tile_info_t
 * g: app_t* 句柄, h: tile_h_t 句柄, p: tile_info_t* 输出 */
#define GUI_WINFO(g,h,p)         appioctl(g, "winfo", (int)(h),(void*)(p))

/* 枚举所有 tile 句柄到 buf
 * g: app_t* 句柄, b: tile_h_t[] 缓冲区, c: int* 输入最大/输出实际数量 */
#define GUI_WENUM(g,b,c)         appioctl(g, "wenum", (void*)(b),(int*)(c))

/* ================================================================
 * 显式渲染（走 trenderall/tredraw/trender 命令）
 *
 * trenderall: 全部可见 tile 按 Z 序全量重绘
 * tredraw:    单 tile 全量重绘
 * trender:    单 tile 增量重绘（obj 数据已由用户准备好）
 *
 * 渲染命令内部调用 kobjsdraw，不做任何 flag 判断，
 * 逐 obj 查 draw_table 分派绘制函数。
 * ================================================================ */

/* 全部可见 tile 按 Z 序重绘
 * g: app_t* 句柄 */
#define GUI_TRENDERALL(g)        appioctl(g, "trenderall")

/* 单个 tile 全量重绘
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_TREDRAW(g,h)         appioctl(g, "tredraw", (int)(h))

/* 单个 tile 渲染（不标脏，由用户控制 obj 状态）
 * g: app_t* 句柄, h: tile_h_t 句柄 */
#define GUI_TRENDER(g,h)         appioctl(g, "trender", (int)(h))

/* ================================================================
 * 绘图原语（作用在 active tile）
 *
 * 这些命令直接写 SPI，不走 obj 系统。
 * 适合静态元素、背景填充等不需要 obj 管理场合。
 * ================================================================ */

/* 用颜色 c 填充 active tile 全部区域
 * g: app_t* 句柄, c: KSCCOLOR */
#define GUI_CLEAR(g,c)           appioctl(g, "clear", (int)(c))

/* 画一像素
 * g: app_t* 句柄, x,y: 坐标, c: KSCCOLOR */
#define GUI_PIXEL(g,x,y,c)       appioctl(g, "pixel", (int)(x),(int)(y),(int)(c))

/* 填充矩形区域
 * g: app_t* 句柄, x,y: 左上, w,h: 宽高, c: KSCCOLOR */
#define GUI_FILL(g,x,y,w,h,c) do { \
    char _b[72]; \
    snprintf(_b, sizeof(_b), "fill -x %d -y %d -w %d -h %d -c %04X", \
        (int)(x), (int)(y), (int)(w), (int)(h), (unsigned)(c)); \
    appcmd((g), _b); \
} while(0)

/* 绘制矩形边框
 * g: app_t* 句柄, x,y: 左上, w,h: 宽高, c: KSCCOLOR */
#define GUI_RECT(g,x,y,w,h,c)    appioctl(g, "rect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))

/* GUI_FILL 别名
 * g: app_t* 句柄, x,y,w,h,c */
#define GUI_FRECT(g,x,y,w,h,c)   appioctl(g, "frect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))

/* 画线段（Bresenham）
 * g: app_t* 句柄, x0,y0,x1,y1: 起止点, c: KSCCOLOR */
#define GUI_LINE(g,x0,y0,x1,y1,c)  appioctl(g, "line", (int)(x0),(int)(y0),(int)(x1),(int)(y1),(int)(c))

/* 画圆形边框
 * g: app_t* 句柄, x,y: 圆心, r: 半径, c: KSCCOLOR */
#define GUI_CIRCLE(g,x,y,r,c)    appioctl(g, "circle", (int)(x),(int)(y),(int)(r),(int)(c))

/* 填充圆形
 * g: app_t* 句柄, x,y: 圆心, r: 半径, c: KSCCOLOR */
#define GUI_FCIRCLE(g,x,y,r,c)   appioctl(g, "fcircle", (int)(x),(int)(y),(int)(r),(int)(c))

/* 画圆弧
 * g: app_t* 句柄, x,y: 圆心, r: 半径, d: 方向(0x0F=全圆), c: KSCCOLOR */
#define GUI_ARC(g,x,y,r,d,c)     appioctl(g, "arc", (int)(x),(int)(y),(int)(r),(int)(d),(int)(c))

/* 画圆角矩形边框
 * g: app_t* 句柄, x,y,w,h: 矩形, r: 圆角半径, c: KSCCOLOR */
#define GUI_RRECT(g,x,y,w,h,r,c) appioctl(g, "rrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))

/* 填充圆角矩形
 * g: app_t* 句柄, x,y,w,h: 矩形, r: 圆角半径, c: KSCCOLOR */
#define GUI_FRRECT(g,x,y,w,h,r,c) appioctl(g, "frrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))

/* 画单字符
 * g: app_t* 句柄, x,y: 左上, ch: 字符, fg: 前景色, bg: 背景色 */
#define GUI_CHAR(g,x,y,ch,fg,bg) do { \
    char _b[72]; \
    snprintf(_b, sizeof(_b), "char -x %d -y %d -v %d -c %04X -b %04X", \
        (int)(x), (int)(y), (unsigned char)(ch), (unsigned)(fg), (unsigned)(bg)); \
    appcmd((g), _b); \
} while(0)

/* 画字符串
 * g: app_t* 句柄, x,y: 左上, s: const char* 字符串, fg: 前景色, bg: 背景色 */
#define GUI_STRING(g,x,y,s,fg,bg) appioctl(g, "string", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#if __USE_CHINESE__
/* 画中文字符串（需 __USE_CHINESE__）
 * g: app_t* 句柄, x,y: 左上, s: UTF-8 字符串, fg,bg: 前景/背景色 */
#define GUI_STRCN(g,x,y,s,fg,bg) appioctl(g, "strcn", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#endif

/* GUI_FILL 别名 */
#define GUI_FILLBOX(g,x,y,w,h,c) GUI_FILL(g,x,y,w,h,c)

/* ================================================================
 * 图像（直通 SPI 快速路径）
 *
 * 数据直接从 flash/ram 经 DMA 发送，不经 obj 系统。
 * ================================================================ */

/* 全彩图像 (16bpp, big-endian 字节流)
 * g: app_t* 句柄, x,y: 左上, w,h: 宽高, d: const uint8_t* 像素数据 */
#define GUI_IMAGE(g,x,y,w,h,d)   appioctl(g, "image", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d))

/* 缩放图像（每像素放大 s 倍）
 * g: app_t* 句柄, x,y: 左上, w,h: 源图宽高, s: 缩放倍数, d: 像素数据 */
#define GUI_IBIG(g,x,y,w,h,s,d)  appioctl(g, "ibig", (int)(x),(int)(y),(int)(w),(int)(h),(int)(s),(const uint8_t*)(d))

/* 二值图像（1bpp, 0=bg, 1=fg）
 * g: app_t* 句柄, x,y: 左上, w,h: 宽高, d: 位图数据, fg,bg: 前/背景色 */
#define GUI_IBIN(g,x,y,w,h,d,fg,bg) appioctl(g, "ibin", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d),(int)(fg),(int)(bg))

/* ================================================================
 * obj 对象系统
 *
 * ksc_obj_t 是纯数据容器，kscgui 不读取任何字段语义。
 * _type 低4位 = draw_table[16] 索引，高4位用户自由定义。
 *
 * 推荐工作流:
 *   1. 申请/准备 ksc_obj_t[] 数组（用户管理内存）
 *   2. GUI_SETOBJPOOL：注册到 tile
 *   3. 填充 obj 字段（sdx/sdy/width/height/colorck/data/_type）
 *   4. GUI_DRAWOBJS：经 appwrite 高频绘制
 *
 * 绘制函数槽可替换:
 *   GUI_SETDRAWFUNC(gui, idx, my_fn)
 *   槽 13-15 预留用户自定义，内置槽也可覆盖。
 * ================================================================ */

/* 绘制单个 obj（高频，走 appwrite）
 * g: app_t* 句柄, o: const ksc_obj_t* 指针
 * 内部查 draw_table[o->_type & 0x0F] 分派 */
#define GUI_DRAWOBJ(g,o)         appwrite(g, (void*)(o), 1, 0x01)

/* 绘制 obj 数组（高频，走 appwrite）
 * g: app_t* 句柄, o: const ksc_obj_t* 数组, n: 数量
 * 内部逐 obj 查 draw_table 分派 */
#define GUI_DRAWOBJS(g,o,n)      appwrite(g, (void*)(o), (uint32_t)(n), 0x02)

/* 注册 obj 池到 tile（低频，走 ioctl）
 * g: app_t* 句柄, h: tile_h_t, o: ksc_obj_t* 数组, n: 数量
 * 注册后 tile->win.objbuf/objnum 指向此数组，
 * GUI_TREDRAW/trenderall 使用此地址 */
#define GUI_SETOBJPOOL(g,h,o,n) do { \
    (g)->mode_data = (void*)(uintptr_t)(h); \
    (g)->user_data = (void*)(o); \
    char _b[32]; \
    snprintf(_b, sizeof(_b), "setobjpool -n %d", (int)(n)); \
    appcmd((g), _b); \
} while(0)

/* 读取 tile 的 obj 池指针和数量（低频，走 ioctl）
 * g: app_t* 句柄, h: tile_h_t, c: int* 输出数量
 * 返回 ksc_obj_t* 指针 */
#define GUI_GETOBJPOOL(g,h,c)    (ksc_obj_t*)(intptr_t)appioctl(g, "getobjpool", (int)(h), (int*)(c))

/* 替换 draw_table 中 idx 槽位的绘制函数（低频，走 ioctl）
 * g: app_t* 句柄, i: 0-15 槽位, f: draw_fn 函数指针
 * 自定义函数签名: void fn(k_draw_device*, KSC_window*, ksc_obj_t*) */
#define GUI_SETDRAWFUNC(g,i,f)   appioctl(g, "setdrawfunc", (int)(i), (void*)(f))

#endif
