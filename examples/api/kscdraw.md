# KSCdraw API

> 声明: `inc/KSCdraw.h` · 实现: `src/KSCdraw.c` / `src/KSCfont.c` / `src/KSCimg.c`

KSCdraw 是一套屏幕抽象：硬件侧通过 `k_draw_device` 提供 SPI 透明接口，软件侧让多个 App 共享 `KSC_window` + `ksc_obj_t[]` 渲染系统。在 PC 平台上由 easyx 呈现，在 STM32 上由 `super_spi → ST7789` 呈现。

## 数据类型

### `KSCCOLOR`

```c
#define KSCCOLOR uint16_t
```

RGB565 16-bit。本配置 `__LITTLE_END_COLOR__ == 1`（ST7789 16位接口工作于小端）：

| 宏 | 值 | 含义 |
|----|-----|------|
| `rred` | `0x00F1` | 红 |
| `ggreen` | `0xE007` | 绿 |
| `bblue` | `0x1F00` | 蓝 |
| `bblack` | `0x0000` | 黑 |
| `wwhite` | `0xFFFF` | 白 |
| `yyellow` | `0xE0FF` | 黄 |

也可用标准 RGB565 常量（大端字节序下与以上等价的值）：`0xF800`(纯红)、`0x07E0`(纯绿)、`0x001F`(纯纯蓝)。

像素位布局（RGB565）：

```
bit 15..11 = R4 R3 R2 R1 R0   (5 bits)
bit 10.. 5 = G5 G4 G3 G2 G1   (6 bits)
bit  4.. 0 = B4 B3 B2 B1 B0   (5 bits)
```

### `uintxy` / `intxy`

```c
#define uintxy uint16_t
#define intxy  int16_t
```

屏幕坐标与尺寸的基础类型。16-bit 足够 240×320。

### `k_draw_device` — 硬件接口抽象

```c
typedef void (*SCR_INIT)(void* data);
typedef void (*SCR_SETCANVAS)(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
typedef void (*SCR_SETCOLORPIXELS)(void* data, const KSCCOLOR* color, uint16_t num);
typedef void (*SCR_WINDOW_SETCANVAS)(k_draw_device* dev, KSC_window* screen, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
typedef void (*SCR_WINDOW_SETPIXELS)(k_draw_device* dev, KSC_window* screen, const KSCCOLOR* color, uint16_t num);

typedef struct k_draw_device {
    void*       data;            /* 硬件 / 驱动上下文 */
    SCR_INIT    init;
    SCR_SETCANVAS      setcanvas;
    SCR_SETCOLORPIXELS setcolorpixels;
    SCR_WINDOW_SETCANVAS setwindows;
    SCR_WINDOW_SETPIXELS setpixels;
} k_draw_device;
```

| 函数指针 | 调用上下文 | 说明 |
|---------|-----------|------|
| `init(data)` | 屏驱动初始化 | 由实现注册的硬件复位 + 初始化序列 |
| `setcanvas(data, Gx,Gy,w,h)` | 裸屏 | 设置写入窗口 (无裁剪) |
| `setcolorpixels(data, color, num)` | 裸屏 | 在当前 canvas 内顺序填入像素 |
| `setwindows(dev, screen, Gx,Gy,w,h)` | KSC_window 级 | 结合窗口相对坐标设置 canvas |
| `setpixels(dev, screen, color, num)` | KSC_window 级 | 结合窗口相对坐标填像素（含裁剪） |

### `KSC_window` — 软件窗口对象

```c
typedef struct KSC_window {
    ksc_obj_t* objbuf;    /* 对象缓冲区，NULL 表示空 */
    KSCCOLOR   bk;         /* 背景色 */
    uintxy    width, height;
    uintxy    ssx, ssy;   /* 在屏幕上的左上角坐标 (Screen-Slot X/Y) */
    uint8_t   Mode;        /* 由用户自定义模式位 */
    uint8_t   objnum;      /* objbuf 中已使用的对象数 */
} KSC_window;
```

`ssx/ssy` 是该窗口在物理屏幕上的偏移，使所有绘图坐标都转化为相对窗口左上角。

### `ksc_obj_t` — 可绘制对象描述

```c
typedef struct ksc_obj_t {
    void*     data;       /* 像素缓冲 / 字符串 / 命令参数 */
    KSCCOLOR  colorck;    /* 前景色 */
    ku8       width, height;
    ku8       sdx, sdy;   /* 屏幕坐标（相对窗口） */
    ku8       d_and_r;    /* 高 5 位: 用户私有; 低 3 位: 保留 */
    ku8       _type;      /* [高 4 位: 用户私有] [低 4 位: draw_table 索引] */
} ksc_obj_t;
```

- `_type & _type_mask` (低 4 位) 索引 `draw_table`，决定 `kobjdraw` 调用哪个 `draw_fn`
- `_type` 高 4 位和 `d_and_r` 高 5 位完全由用户私有
- KSCGUI 的 Tile 系统也按本结构管对象，见 [`apps.md` § KSCGUI](apps.md#kscgui)

## draw_table — 默认索引

```c
#define _fillbox       0
#define _box           1
#define _line          2
#define _string        3
#define _image         4
#define _imagebig      5
#define _ibin          6
#define _circle        7
#define _fillcircle    8
#define _arc           9
#define _roundrect    10
#define _fillroundrect 11
#define _char         12
#define _rect         _box
#define _fillrect     _fillbox

#define _type_mask   (0x0F)
#define _r_mask      (0x1F)
#define _d_mask      (0xE0)
```

`kobjdraw_init(dev)` 按以上表把默认 `draw_fn` 装到 `dev->draw_table[]`。用户扩展时调 `ksc_set_draw_func(idx, fn)` 覆盖既存槽，或选 idx ≥ 12（但默认 4-bit 限制最多 16 个槽，可改 `_type_mask` 拓宽）。

### draw_fn 签名与扩展

```c
typedef void (*draw_fn)(k_draw_device* dev, KSC_window* screen, struct ksc_obj_t* obj);

void kobjdraw_init(k_draw_device* dev);                    /* 装载默认 draw_table */
int  ksc_set_draw_func(uint8_t idx, draw_fn fn);           /* 覆盖 / 注册槽 */
draw_fn ksc_get_draw_func(uint8_t idx);                    /* 查槽 */
```

## 管理函数

### 设备查找 / 创建

```c
k_draw_device* k_draw_device_init(void);
k_draw_device* k_draw_device_find(const char* app_name);
```

`k_draw_device_init` 一次性创建设备表（默认包含 easyx / ST7789 等，按 `__USE_*` 配置）；`k_draw_device_find` 按名字查找。

### 窗口挂载

```c
k_draw_device* kscreenmount(void);
KSC_window*    kscreeninit(k_draw_device* dev, uintxy ssx, uintxy ssy,
                            uintxy width, uintxy height, KSCCOLOR bk);
void           kscreenfree(k_draw_device* dev, KSC_window* screen);
```

- `kscreenmount` 取当前活动的 `k_draw_device*`
- `kscreeninit` 分配并初始化 `KSC_window`（含 objbuf=NULL）
- `kscreenfree` 释放窗口（含 objbuf）

## 渲染 API

| API | 说明 |
|-----|------|
| `kscreenclear(dev, screen)` | 用 `bk` 填满窗口 |
| `kobjdraw(dev, screen, obj)` | 渲染单个对象（按 `_type` 派发到 `draw_table`） |
| `kobjsdraw(dev, screen, obj, num)` | 顺序渲染 `num` 个对象 |
| `kscreendraw(dev, screen)` | 渲染 `screen->objbuf` 中前 `objnum` 个对象 |

## 基本绘图函数

签名链式风格：`(dev, win, color, ...) → KSC_mes`。迷茫时 `KSC_ERR = 1` 为常规错（越界 / 参数非法），`KSC_ERR_OUT_OF_RANGE = 2` 为坐标超窗。

```c
typedef enum KSC_mes {
    KSC_OK = 0,
    KSC_ERR = 1,
    KSC_ERR_OUT_OF_RANGE = 2,
} KSC_mes;
```

| 函数 | 签名 | 说明 |
|------|------|------|
| `ksetpixel` | `(dev, win, color, x, y)` | 单像素点 |
| `kfull` | `(dev, win, color, x1, y1, w, h)` | 填充矩形（区域） |
| `kbox` / `krect` | `(dev, win, color, x1, y1, w, h)` | 矩形边框 (4 条边) |
| `kfillbox` / `kfillrect` | `(dev, win, color, x1, y1, w, h)` | 填充矩形 |
| `kline` | `(dev, win, color, x1, y1, x2, y2)` | 直线 (Bresenham) |
| `kcircle` | `(dev, win, color, x0, y0, r)` | 圆中点画圆算法 |
| `kfillcircle` | `(dev, win, color, x0, y0, r)` | 填充圆 |
| `karc` | `(dev, win, color, x0, y0, r, Anglediraction)` | 圆弧, `Anglediraction` 为 4-bit: `0x01`=右上, `0x02`=左上, `0x04`=右下, `0x08`=左下 |
| `kroundrect` | `(dev, win, color, x1, y1, w, h, r)` | 圆角矩形边框 |
| `kfillroundrect` | `(dev, win, color, x1, y1, w, h, r)` | 填充圆角矩形 |
| `kdrawimage` | `(dev, win, img, x, y, w, h)` | RGB565 像素图 (uint16*) |
| `kdrawimagebig` | `(dev, win, img, x, y, w, h, scale)` | 整数倍缩放图 |
| `kimagebin` | `(dev, win, img, x, y, w, h, colorck, colorbk)` | 1-bit 二值图 |
| `kchar` | `(dev, win, ch, x, y, colorck, colorbk)` | 单 ASCII 字符 (8×16) |
| `kstring` | `(dev, win, str, x, y, colorck, colorbk)` | ASCII 字符串 |
| `kstringchinese` | `(dev, win, str, x, y, color1, color2)` | 中文字符串，需 `__USE_CHINESE__=1` |

> 圆形系列需 `__DRAW_CIRCLE__ == 1`，否则函数未编译。

## 坐标系

- 所有坐标都是窗口左上角原点 `(0,0)`，向右 +x、向下 +y。
- `ssx/ssy` 在 `KSC_window` 创建时给定物理屏左上角，使多窗口可以叠加在屏上不同位置。
- KSCGUI 的 Tile 系统在 ST7789 上以 240×160 (本配置) 或 240×320 (ST7789 物理屏) 划分多个不重叠的 Tile，详见 [`apps.md` § KSCGUI](apps.md#kscgui)。

## draw_table 扩展示例

```c
/* 自定义对象类型 idx=13 (即 _type 低 4 位 =13) */
static void draw_my_flag(k_draw_device* dev, KSC_window* win, ksc_obj_t* obj) {
    /* obj->data / obj->colorck / obj->width / obj->height / obj->sdx/sdy 均可用 */
    kfillbox(dev, win, obj->colorck,
             obj->sdx, obj->sdy, obj->width, obj->height);
    kbox(dev, win, ~obj->colorck & 0xFFFF,
         obj->sdx+1, obj->sdy+1, obj->width-2, obj->height-2);
}

ksc_set_draw_func(13, draw_my_flag);  /* 装进 draw_table[13] */

/* 创建一个用本类型的对象 */
KSC_window win;
ksc_obj_t obj = {
    .data = NULL,
    .colorck = rred,
    .width = 20, .height = 10,
    .sdx = 30, .sdy = 30,
    ._type = 13,   /* 低 4 位 = 13 触发 draw_my_flag */
};
kobjdraw(dev, &win, &obj);   /* 渲染 */
```

## 与 KSCGUI 的关系

KSCGUI 的 Tile 持有 `KSC_window` + `ksc_obj_t[]`，并提供 `wcreate / wselect / wmove / tredraw / ...` 等 appcmd；调用者通过 appcmd 创建 Tile，内部间接调用以上 API。详见模块根 `README.md` "应用清单" 与 `docs/KSCGUI_API.md`（仍可参考，命令清单以源码为准）。