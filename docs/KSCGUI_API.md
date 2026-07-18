> **⚠️ 过时提示:** 本文档部分内容（如 `appioctl` 调用、`app_dep` 双 SPI 描述）已与当前 `appcmd` 框架不一致。最新 API 契约请参考 `examples/api/apps.md` 和 `apps/kscgui.c` 源码。

# KSCGUI — ioctl 命令参考

## 概述

KSCGUI 注册于 app 框架，通过 `appioctl()` 接受命令。
所有绘图基于 KSCdraw 引擎，输出至 super_spi1/2 → ST7789（240×320）。
支持多窗口（最多 4 个）和用户管理的对象渲染系统。

调用方式：

```c
app_t* gui = appget("KSCGUI");
appopen(gui);
appioctl(gui, "setspi", 2);   // optional, default=SPI2
appioctl(gui, "init");
appioctl(gui, "clear", 0x0000);
```

---

## 应用注册信息

| 字段 | 值 |
|------|-----|
| 注册名 | `"KSCGUI"` |
| 依赖字符串 | `app_dep: "2\0super_spi1\0super_spi2"`（app0=super_spi1, app1=super_spi2） |
| 屏幕 | ST7789, 240×320 竖屏 |
| 依赖打开时机 | `open` 时默认打开 SPI2（若不可用则回退 SPI1）；可通过 `"setspi"` 运行时切换 |

---

## 颜色值约定

颜色参数类型 `KSCCOLOR` = `uint16_t`，RGB565 位布局：

```
15 14 13 12 11 | 10  9  8  7  6 | 5  4  3  2  1  0
 R4  R3 R2 R1 R0| G5 G4 G3 G2 G1| B4 B3 B2 B1 B0
```

宏定义于 `KSCconfig.h`（`__LITTLE_END_COLOR__ == 1` 时）：

| 宏 | 值 |
|----|-----|
| `rred` | `0x00F1` |
| `ggreen` | `0xE007` |
| `bblue` | `0x1F00` |
| `bblack` | `0x0000` |
| `wwhite` | `0xFFFF` |
| `yyellow` | `0xE0FF` |

也可直接用标准 RGB565 常量：`0xF800`（纯红）、`0x07E0`（纯绿）、`0x001F`（纯蓝）。

---

## 命令列表

### `"init"`
初始化 ST7789：硬件复位 → 送初始化序列（SLPOUT、COLMOD、MADCTL、伽马等）。
**每次上电后调用一次。**

```c
appioctl(gui, "init");
```

---

### `"clear"`
全屏填充单一颜色。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | color | KSCCOLOR 填充色 |

```c
appioctl(gui, "clear", 0x0000);
```

---

### `"pixel"`
画一个像素点。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 列坐标 (0..239) |
| arg2 | y | 行坐标 (0..319) |
| arg3 | color | 颜色 |

```c
appioctl(gui, "pixel", 10, 20, 0xFFFF);
```

---

### `"fill"`
填充矩形区域。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 (像素) |
| arg4 | h | 高度 (像素) |
| arg5 | color | 填充色 |

```c
appioctl(gui, "fill", 50, 50, 100, 80, 0xF800);
```

---

### `"rect"`
绘制矩形边框（空心）。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 |
| arg4 | h | 高度 |
| arg5 | color | 线条色 |

```c
appioctl(gui, "rect", 10, 10, 60, 60, 0xFFFF);
```

---

### `"line"`
绘制线段。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x0 | 起点列 |
| arg2 | y0 | 起点行 |
| arg3 | x1 | 终点列 |
| arg4 | y1 | 终点行 |
| arg5 | color | 线条色 |

```c
appioctl(gui, "line", 0, 0, 239, 319, 0x07E0);
```

---

### `"circle"`
绘制圆形边框（空心）。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | cx | 圆心列 |
| arg2 | cy | 圆心行 |
| arg3 | r | 半径 |
| arg4 | color | 线条色 |

```c
appioctl(gui, "circle", 120, 160, 50, 0xFFFF);
```

---

### `"fcircle"`
绘制实心圆。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | cx | 圆心列 |
| arg2 | cy | 圆心行 |
| arg3 | r | 半径 |
| arg4 | color | 填充色 |

```c
appioctl(gui, "fcircle", 120, 160, 50, 0x07E0);
```

---

### `"arc"`
绘制圆弧（象限扇区）。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | cx | 圆心列 |
| arg2 | cy | 圆心行 |
| arg3 | r | 半径 |
| arg4 | dir | 象限掩码（见下） |
| arg5 | color | 线条色 |

`dir` 组合：

| 标志 | 值 | 含义 |
|------|-----|------|
| `RightUpper` | `0x01` | 右上 0°~90° |
| `LeftUpper` | `0x02` | 左上 90°~180° |
| `RightLower` | `0x04` | 右下 270°~360° |
| `LeftLower` | `0x08` | 左下 180°~270° |

示例：`0x01 | 0x02` 画上半圆；`0x05` 画右半圆。

```c
// 上半圆
appioctl(gui, "arc", 120, 160, 50, 0x01 | 0x02, 0xFFFF);
```

---

### `"rrect"`
绘制圆角矩形边框。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 |
| arg4 | h | 高度 |
| arg5 | r | 圆角半径 |
| arg6 | color | 线条色 |

```c
appioctl(gui, "rrect", 20, 20, 200, 280, 10, 0xFFFF);
```

---

### `"frrect"`
绘制实心圆角矩形。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 |
| arg4 | h | 高度 |
| arg5 | r | 圆角半径 |
| arg6 | color | 填充色 |

---

### `"char"`
绘制单个 ASCII 字符（8×8 系统字体）。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | ch | ASCII 码 |
| arg4 | fg | 前景色（像素 1） |
| arg5 | bg | 背景色（像素 0） |

```c
appioctl(gui, "char", 10, 10, 'A', 0xFFFF, 0x0000);
```

---

### `"string"`
绘制 ASCII 字符串。字符间自动推进 x 坐标（间距 7px）。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 起始列 |
| arg2 | y | 起始行 |
| arg3 | s | `const char*` 字符串指针 |
| arg4 | fg | 前景色 |
| arg5 | bg | 背景色 |

```c
appioctl(gui, "string", 10, 300, "Hello", 0xFFFF, 0x0000);
```

---

### `"image"`
1:1 原尺寸绘制图片。数据为原始字节流，不经过 `uint16_t` 强制转换，直接逐批 memcpy 送入 SPI。

**图片数据格式：** 每像素 2 字节，**大端序**（高位在前，低位在后）。此即 ST7789 SPI 字节顺序。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 (像素) |
| arg4 | h | 高度 (像素) |
| arg5 | img | `const uint8_t*` 像素数据指针 |

```c
extern const uint8_t OneSleepWorm[3200]; // KSCimg.h
appioctl(gui, "image", 0, 0, 40, 40, OneSleepWorm);
```

---

### `"ibig"`
缩放绘制图片。逐像素 `(img[0]<<8)|img[1]` 大端显式读出，构造 KSCCOLOR 后调用 kfull 填充。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 源图宽度 |
| arg4 | h | 源图高度 |
| arg5 | s | 缩放倍数（1 = 原大, 2 = 2×, 3 = 3×…） |
| arg6 | img | `const uint8_t*` 像素数据指针 |

```c
appioctl(gui, "ibig", 10, 10, 40, 40, 3, OneSleepWorm);
```

性能参考：40×40 × 3 倍 = 1600 次 kfull 调用，每像素一个 DMA 传输。

---

### `"ibin"`
绘制 1-bit 二值图。用 `imgchange()` 解压为 KSCCOLOR 数组后走 `"image"` 路径。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | x | 左上角列 |
| arg2 | y | 左上角行 |
| arg3 | w | 宽度 |
| arg4 | h | 高度 |
| arg5 | img | `const uint8_t*` 位图数据（每像素 1 bit） |
| arg6 | fg | 前景色（bit=1） |
| arg7 | bg | 背景色（bit=0） |

```c
extern const uint8_t Wechat[]; // KSCimg.h, 16×16 二值图标
appioctl(gui, "ibin", 10, 10, 16, 16, Wechat, 0xFFFF, 0x0000);
```

---

### `"orient"`
设置 MADCTL 寄存器，切换屏幕方向。

| 参数 | 类型 | 说明 |
|------|------|------|
| arg1 | mode | MADCTL 值，直接写入 0x36 |

常用值：

| mode | 效果 |
|------|------|
| `0x00` | 竖屏（默认，240×320） |
| `0x60` | 横屏（320×240） |
| `0xC0` | 竖屏翻转 |
| `0xA0` | 横屏翻转 |

```c
appioctl(gui, "orient", 0x60); // 横屏
```

---

## 语法糖宏

`kscgui.h` 提供的宏将上述命令封装为函数式调用：

```c
#define GUI_INIT(g)          appioctl(g, "init")
#define GUI_CLEAR(g,c)       appioctl(g, "clear", (int)(c))
#define GUI_PIXEL(g,x,y,c)   appioctl(g, "pixel", (int)(x),(int)(y),(int)(c))
#define GUI_FILL(g,x,y,w,h,c)     appioctl(g, "fill", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_RECT(g,x,y,w,h,c)     appioctl(g, "rect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_LINE(g,x0,y0,x1,y1,c) appioctl(g, "line", (int)(x0),(int)(y0),(int)(x1),(int)(y1),(int)(c))
#define GUI_CIRCLE(g,x,y,r,c)     appioctl(g, "circle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_FCIRCLE(g,x,y,r,c)    appioctl(g, "fcircle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_ARC(g,x,y,r,d,c)      appioctl(g, "arc", (int)(x),(int)(y),(int)(r),(int)(d),(int)(c))
#define GUI_RRECT(g,x,y,w,h,r,c)  appioctl(g, "rrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_FRRECT(g,x,y,w,h,r,c) appioctl(g, "frrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_CHAR(g,x,y,ch,fg,bg)  appioctl(g, "char", (int)(x),(int)(y),(int)(ch),(int)(fg),(int)(bg))
#define GUI_STRING(g,x,y,s,fg,bg) appioctl(g, "string", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#define GUI_IMAGE(g,x,y,w,h,d)    appioctl(g, "image", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d))
#define GUI_IBIG(g,x,y,w,h,s,d)   appioctl(g, "ibig", (int)(x),(int)(y),(int)(w),(int)(h),(int)(s),(const uint8_t*)(d))
#define GUI_IBIN(g,x,y,w,h,d,fg,bg) appioctl(g, "ibin", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d),(int)(fg),(int)(bg))
#define GUI_ORIENT(g,m)         appioctl(g, "orient", (int)(m))
```

宏仅做参数类型转换 + 转发，不引入额外逻辑。可直接用宏或直接调 `appioctl`，二者等价。

---

## 注意事项

- **必须先 `appget("KSCGUI")` + `appopen()`** 才能发任何命令。
- **`"init"` 仅需一次**。重复调无害但耗时。
- 图片数据固定为**大端序**——与 KSCdraw 算法产生的 KSCCOLOR（小端序 uint16_t）不同。`"image"` 和 `"ibig"` 不经过 `uint16_t*` 强制转换，逐字节处理，无字节序问题。
- 栈空间 1KB，勿在栈上声明大数组。`"string"` 的 `s` 参数指向外部缓冲区，不会复制。
- 所有颜色参数按 `int` 类型传递，ioctl 内通过 `va_arg(ap, int)` 读取，然后低位截断为 `KSCCOLOR`。颜色值高 16 位被忽略。
