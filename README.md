# KSCOS

KSCOS 是一个跨平台、轻量级、**非 RTOS 式**的应用框架，面向 PC / STM32 / ESP32 三类目标，统一以「App + 字符串命令 (appcmd)」的方式组织外设驱动、GUI、文件系统与应用逻辑。它不使用任务调度器，也不依赖 HAL 的回调与中间件——所有外设要么直接操作寄存器，要么以纯软件 KSCdraw 引擎呈现。

- **应用框架:** `REGISTER_APP` 把模块塞进 `app_table` ELF 段，运行时用 `appget/appopen/appread/appwrite/appcmd` 五个 API 统一访问，依赖通过 `app_dep` 字符串递归解析。
- **字符串接口:** `appcmd(app, "cmd -x val -y val")` 是所有 App 的统一入口；带引号 / 转义解析，支持 `user_data` / `output_fn` 双向数据通道与持久句柄。
- **图形引擎:** `KSCdraw` 提供 `k_draw_device` 抽象 + `KSC_window` + 可扩展的 `draw_table`，在 PC 上由 easyx 呈现，在 STM32 上由 `super_spi → ST7789` 直出。
- **多平台:** `KSCconfig.h` 中 `__USE_PC__/__USE_STM32__/__USE_ESP32__` 三选一——同一份源码可以在 PC 上由 easyx 呈现、在 STM32 上由 SPI 直出。
- **可替换实现:** 外部库（littlefs / easyx / tjpgd / HAL 桩）只是某个 App 在某平台下的**具体实现**。开发者只需替换或新增 `REGISTER_APP` 模块，框架自动通过 `app_dep` 串接依赖、由 `target_sources` 决定哪些 App 进入最终镜像——无需改动框架核心，即可完成程序构建。

---

## 目录结构

```
KSCOS/
├── inc/                # 公共头文件
│   ├── app.h            # App 框架 (papp_t / app_t / REGISTER_APP / appcmd)
│   ├── KSCOSsystem.h    # 系统服务 (sys_init / kscprintf / ksc_terminal)
│   ├── kscsystem.h      # 固定 app 内核接口 (SYSTEMAPP / CONSOLEAPP / 命令常量)
│   ├── mempool.h        # 多档块池接口 + 统计结构
│   ├── KSCdraw.h        # 图形引擎 (k_draw_device / KSC_window / ksc_obj_t)
│   ├── KSCconfig.h      # 平台开关 + 颜色宏 + 屏幕配置
│   ├── KSCfont.h        # 字体数据接口
│   ├── KSCimg.h         # 图像数据接口
│   ├── UTF8_FlashN.h
├── src/                # 框架核心实现
│   ├── main.c           # 统一主函数入口 (PC / STM32)
│   ├── app.c            # app系列函数
│   ├── KSCOSsystem.c    # 共享层 (kscprintf/kscterminal/sys_init + 全局定义)
│   ├── KSCdraw.c        # k_draw_device + 对象 draw_table + 基本绘图
│   ├── KSCfont.c
│   └── KSCimg.c
├── apps/               # 应用模块 (平台无关, 每个 .c 一个 REGISTER_APP)
│   ├── app_config.h     # 跨 App 共享类型 
│   ├── kscgui.c         # GUI 组件库
│   ├── list.c           # GUI 列表 widget (含内置键盘控制)
│   ├── snake.c          # Snake 游戏 (中断驱动)
│   ├── littlefs_fs.c    # littlefs on W25Q64
│   ├── terminal.c       # 字符串路由分发器
│   ├── open.c           # 按扩展名路由文件打开 (txt,bmp)
│   └── transfer.c       # XMODEM-128 文件上传
├── bsp/                # 平台实现 (REGISTER_APP + 驱动, 按平台条件编译)
│   ├── share/src/       # 跨平台共享实现
│   │   └── mempool.c    # 多档块池 (内核内存服务)
│   ├── stm32/           # STM32: gpio/uart/super_spi/w25qxx/tim/button16/gui_drv/system
│   ├── pc/              # PC:   uart/w25qxx/tim/button16/gui_drv/system (MinGW + easyx)
│   └── linux/           # Linux: 同 PC 功能 (SDL2 GUI + pthread + 文件模拟, CMake LINUX_NATIVE=ON)
├── cmake/
│   └── gcc-arm-none-eabi.cmake   # arm-none-eabi 工具链配置
├── third_party/
│   ├── easyx/           # PC端gui窗口实现: 静态库 libeasyx.a + graphics.h
│   ├── littlefs/        # littlefs 源 (lfs.c / lfs_util.c / lfs_config.h)
│   ├── stm32/           # CMSIS 头 + 启动汇编 + 链接脚本 + 系统源文件
│   │   ├── inc/CMSIS/        # core_cm3.h 等 Cortex-M3 内核头
│   │   ├── inc/CMSIS_Device/ # stm32f1xx.h, stm32f103xb.h 设备寄存器定义
│   │   ├── startup/          # startup_stm32f103xb.s
│   │   ├── src/              
│   │   └── STM32F103XX_FLASH.ld  # 链接脚本
│   ├── async_xmodem/   # XMODEM 接收端 (transfer app)
│   └── tjpgd3/          # JPEG 解码（暂无实现）
├── examples/           
├── docs/
│   └── api/            # API 参考
├── flash_debug.jlink   # JLink 烧录脚本
├── CMakeLists.txt      # 统一 CMake 入口 (PC / STM32)
├── CMakePresets.json   # 构建预设 (PC Debug/Release, Firmware Debug/Release)
├── build/              # 构建输出 (gitignored)
├── .data/              # 运行时数据 — 见下方警告
└── LICENSE             # MIT, © 2026 OneSleepWorm
```

> **⚠️ 警告：`KSCOS/.data/` 是运行时持久化数据目录，包含 `flash.bin`（littlefs 文件系统镜像）。**
> **任何清理脚本、测试流程、AI agent 都严禁删除或清空此目录及其文件。**
> 删除 `flash.bin` = 销毁所有存储在 littlefs 上的用户文件。仅可在明确确认后手动操作。

---

## 平台开关 (`inc/KSCconfig.h`)

```c
#define __USE_PC__     0   /* 三选一: 1 = PC 调试目标 */
#define __USE_STM32__  1   /* 三选一: 1 = STM32 嵌入式目标 */
#define __USE_ESP32__  0   /* 三选一: 1 = ESP32 目标 (暂未实现) */
```

> **`__USE_LINUX__`** (Linux 原生目标, 由 `LINUX_NATIVE=ON` 注入) 与 `__USE_PC__=1` **同时生效**:
> `__USE_PC__` 语义为"非嵌入式主机端" (决定 apps/ 是否参与编译), `__USE_LINUX__` 只负责
> 选择 `bsp/linux/` 平台层并屏蔽 Win32 头 (`KSCconfig.h` 内已条件化)。

| 开关 | 默认 | 作用 |
|------|------|------|
| `__USE_LCD__` | 1 | 启用 KSCdraw / 屏幕输出 |
| `__USE_ST7789__` / `__USE_ST7735__` | 屏驱动选择 |
| `__USE_CHINESE__` | 0 | 中文字符串渲染 |
| `__DRAW_CIRCLE__` | 1 | 圆形 / 圆弧 / 圆角矩形 API |

PC 构建时由 `CMakePresets.json` 注入 `__USE_PC__=1`，STM32 固件通过 `STM32_FIRMWARE=ON` 注入 `__USE_STM32__=1 STM32F103xB`。

---

## 构建

KSCOS 支持统一 CMake 入口，通过预设切换 PC 调试与 STM32 固件构建。

### STM32 固件构建

依赖 `arm-none-eabi-gcc`（GNU Arm Embedded Toolchain）和 Ninja。

```sh
cd KSCOS/

# Debug 构建
cmake --preset Firmware-Debug && cmake --build --preset Firmware-Debug

# Release 构建
cmake --preset Firmware-Release && cmake --build --preset Firmware-Release

# 产物: build/Release/KSCOS.elf
```

烧录（JLink + SWD）：

```sh
& "D:\SEGGER\JLink_V924a\JLink.exe" -autoconnect 1 -commanderscript flash_debug.jlink
```

`flash_debug.jlink` 执行擦除 → 加载 → 复位 → 运行。也可手动指定：

```
device STM32F103C8 / si SWD / speed 4000 / connect / erase / loadfile ./build/Release/KSCOS.elf / reset / go
```

### PC 调试构建

依赖 MinGW + easyx（`third_party/easyx/libeasyx.a` 已随仓提供）。用于在没有硬件时验证 App 框架与 KSCdraw 算法。

```sh
cd KSCOS/

cmake --preset "PC Debug" && cmake --build --preset "PC Debug"
# 产物: build_debug/KSCOS.exe
```

### Linux 原生构建

依赖系统 gcc + SDL2 (`sudo apt install libsdl2-dev`) 与 Ninja。用于在 Linux 上运行完整框架
(GUI / terminal / littlefs / snake 等全部 App), 产物路径解析: 优先 `KSCOS_DATA_DIR` 环境变量,
否则从可执行文件位置自动回溯到 `KSCOS/.data/`。

```sh
cd KSCOS/

cmake --preset Linux-Debug && cmake --build --preset Linux-Debug
# 产物: build/linux-debug/KSCOS
# GUI 窗口为 240x320 的 SCALE 倍 SDL2 窗口; 串口仍走 .data/stdinN.txt / stdoutN.txt
```

### 预设一览

| 预设 | 目标 | 生成器 | 工具链 |
|------|------|--------|--------|
| `PC Debug` | PC 调试 | MinGW Makefiles | gcc/g++ (MinGW) |
| `PC Release` | PC 发布 | MinGW Makefiles | gcc/g++ (MinGW) |
| `Linux-Debug` | Linux 原生调试 | Ninja | 系统 gcc + SDL2 + pthread |
| `Linux-Release` | Linux 原生发布 | Ninja | 系统 gcc + SDL2 + pthread |
| `Firmware-Debug` | STM32 调试 ( -Os -g3 ) | Ninja | arm-none-eabi-gcc |
| `Firmware-Release` | STM32 发布 ( -Os -g0 ) | Ninja | arm-none-eabi-gcc |
---

## 核心架构: App 框架

### 类型 (`inc/app.h`)

`papp_t` 是**编译期常量**——由 `REGISTER_APP` 放入 `app_table` ELF 段; `app_t` 是**运行期实例**，由 `appget` 在首次查找时 `osmalloc` 出来，塞进单例缓存。

```
papp_t (const, section "app_table")
├── base.app_name      const char*
├── dep_str            "0"          (pdrv 依赖, 已废除, 一律填 "0")
├── app_dep_str        "N\0name1\0name2\0..."   (App 间依赖)
└── ops                open/close/read/write/cmd

app_t (runtime, osmalloc)
├── papp        -> papp_t*
├── app0..app3  -> 依赖的 app_t*  (由 app_dep_str 递归 appget 填充)
├── callback    void_func_t           事件回调
├── output_fn / output_ctx            输出重定向 (流式回调)
├── app_data    void*                 App运行时上下文数据
├── user_data   void*                 调用方数据指针 
├── callback_data  void*              返回数据指针 
└── mode_data  void*                  当前模式缓存
```

### 注册宏

```c
REGISTER_APP(name, dep, app_dep, ops, desc)       // app_dep = NULL 的简写
REGISTER_APP_EX(name, dep, app_dep, ops, desc)   // 完整版
```

展开为:
```c
static const papp_base_t _APP_BASE_xxx = { name };
static const papp_t _APP_DEF_xxx
    __attribute__((section("app_table"), used)) = { base, dep, app_dep, ops };
```

链接器要求: **`REGISTER_APP` 文件必须出现在 `target_sources(...)` 而非 `add_library(...)`**——否则链接器会丢弃 orphan section 符号, `app_table` 不会被放入最终镜像。(参见 `apps/CMakeLists.txt`)

### 依赖解析

`app_dep_str` 字符串格式: `"N\0name1\0name2\0..."`

- 第一字符 `N` = 依赖个数 (`'1'`~`'4'`)
- 第二字节起是 `\0` 分隔的 App 名, 每段最大 4 个

`appget` 在创建 `app_t` 时按顺序 `appget` 这些名字, 填入 `app0..app3`。`N > 4` 会被截到 4。任一依赖查不到, 则整体 `appget` 失败 (返回 NULL)。

### 公共 API

| API | 签名 | 说明 |
|-----|------|------|
| `appget` | `app_t* appget(const char* name)` | 查缓存→查 `papp_table`→`osmalloc(app_t)` 并解析 `app_dep`→入缓存。每次调用让 `get_refs++` |
| `appopen` | `int appopen(app_t* app)` | 幂等: 若未 open 则调用 `ops->open`。`app_data` 在此分配 |
| `appclose` | `int appclose(app_t* app)` | 调用 `ops->close` (释放 `app_data`)。**不做引用计数** —— 一旦 close, 上下文销毁 |
| `appread` | `int appread(app_t* app, void* data, uint32_t count, uint32_t mode)` | 读路径 (键码 / 像素 / 文件字节) |
| `appwrite` | `int appwrite(app_t* app, void* data, uint32_t count, uint32_t mode)` | 写路径 (字节发送 / 引脚 / SPI 帧) |
| `appcmd` | `int appcmd(app_t* app, const char* cmdline)` | 字符串命令解析 + 调用 `ops->cmd(app, name, argv)` |
| `appcmd_argv` | `int appcmd_argv(app_t* app, const char* cmdname, const char** argv)` | 直接给已解析 argv, 跳过字符串解析 |
| `appfree` | `void appfree(app_t* app)` | `get_refs--`; 当前实现未真正释放 (TODO) |

### 单例 + 引用计数

- `_app_cache` 单向链表, 每条节点 `{app*, get_refs, app_state, next}`。
- `appget` 命中缓存 → `get_refs++` 返回; 未命中 → malloc 新节点入链表头。
- `appopen` 幂等: 第二次 open 不再调用 `ops->open`。
- `appclose` **没有引用计数**: 一旦调用 `ops->close`, 缓存里那条记录的 `APP_STATE_OPENED` 即清掉, `app_data` 被 close 释放。**多块代码共享同一 App 时务必协调 open/close 时机**, 否则一者 close 会让另一者的上下文失效。

---

## `appcmd` 字符串接口

### 语法

```
cmd -x val1 -y val2          flag 顺序任意
cmd                          无参数命令
```

- 第一个 token 为命令名 (`cmdname`), 后续以 `-<a-z>` 形式记 flag, 共 26 个槽 (`a..z`)。
- `app->app_ops->cmd(app, cmdname, argv)`: `argv[0..25]` 中对应槽位为字符串指针, 未出现的 flag 槽位为 `NULL`。

### 宏

```c
APPCMD_HAS(argv, 'x')   // argv['x'-'a'] != NULL  —— flag 是否出现
APPCMD_ARG(argv, 'x')    // argv['x'-'a']          —— 取值字符串
```

### 引号与转义 (由 `app()::appcmd()` 统一解码)

| 方式 | 示例 | 说明 |
|------|------|------|
| 无引号 | `-d hello` | 值遇空格 / 行尾结束, 不支持值内空格 |
| 双引号 | `-d "hello world"` | 支持转义序列 (见下), 遇未转义 `"` 结束 |
| 单引号 | `-d 'hello world'` | 纯字面, 不支持转义, 遇 `'` 结束 |

双引号内支持的转义:

| 序列 | 解码 |
|------|------|
| `\"` | `"` (0x22) |
| `\\` | `\` (0x5C) |
| `\n` | LF (0x0A) |
| `\r` | CR (0x0D) |
| `\t` | TAB (0x09) |
| `\0` | NUL (0x00) |
| `\x` (其它) | 保留字面 `\x` |

> 命令行由内部 `static char buf[128]` 解析, 转义解码就地完成——不会溢出。**注意**: `appcmd` 用静态 buffer, 非可重入; 中断上下文中请改用 `appcmd_argv`。

### 数据通道

| 字段 | 用途 | 管理者 |
|------|------|--------|
| `app_data` | 内部状态上下文 | 各 App 自身 (open 时 osmalloc) |
| `user_data` | 命令输入 / 输出数据指针 | 调用方 |
| `output_fn` / `output_ctx` | 流式输出回调 | 调用方 / terminal |
| `callback` | 事件通知 (RX 到达 / 列表选中) | 注册方 |
| `callback_data` | 持久句柄 (文件 fd / tile handle) | App 内部 |
| `mode_data` | 当前模式 / 缓存数据 | App 内部 |

输出类命令 (`cat`/`ls`/`read`/`info`/`stat`/`uid`) 的优先级:
1. `app->user_data ≠ NULL` → 写入 `user_data` (程序内捕获)
2. `app->output_fn ≠ NULL` → 逐块调用 (流式)
3. 两者皆 NULL → 返回 `-1`

### 返回值约定

- `≥ 0`: 成功 (具体语义随命令: 字节数 / handle / 状态)
- `< 0`: 错误

### 实例 + 操作 mode 字节编码

许多 App 用一个字节携带硬件实例号与操作码:

```
mode = (inst << 4) | op
        inst: 1-indexed 硬件实例 (USART1/2/3, TIM1..4, SPI1/2)
        op:   低 nibble 操作码
              0=noop 1=set/period 2=start/stop 4=init 5=register ...
```

详见各 App 头注释。

### Hello-World 示例

```c
    appget("system");      /* 固定起手招: 内核服务 */
    appget("console");     /* 固定起手招: printf/终端 */
    sys_init();
    app_t* term = appget("terminal");
    if (term) appopen(term);
 while (1) {
     uint8_t c;
     while (appread(CONSOLEAPP, &c, 1, 1) > 0)
        appwrite(term, &c, 1, 0);
     }
```

---

## 系统服务 (`inc/KSCOSsystem.h` + `inc/kscsystem.h`)

| API | 说明 |
|-----|------|
| `SYSTEMAPP` / `CONSOLEAPP` | 固定地址 app 句柄 (libc/stdio), `appget("system")` / `appget("console")` 自动 open |
| `sys_init()` | 引导装配: `appget("uart_serial")`+open+`appcmd "open"` → `appget("console")`+open → `appget("terminal")`+open |
| `sysdelay(ms)` / `sysgettime()` | 经 SYSTEMAPP 分发的时间服务 (fastsystem.h 宏) |
| `os_malloc(size)` / `os_free(p)` / `os_calloc(n,sz)` | fastsystem.h 内联宏, 经 SYSTEMAPP → mempool (旧名 `osmalloc` 等兼容) |
| `kscprintf(fmt, ...)` | 经 CONSOLEAPP 输出格式化串 (含 `vsnprintf`, buffer 128B) |
| `kscterminal()` | 经 CONSOLEAPP 读输入 → terminal app 路由 |

> 全局 `ksc_console` / `ksc_term` 变量已废除，由固定地址 `CONSOLEAPP` 取代。正式接口只有：app 系列函数 + SYSTEMAPP + CONSOLEAPP。

### 强制规则: 所有运行期上下文必须 osmalloc

> **栈只有 1KB (`0x400`)**——任何稍大的局部数组 / 缓冲会无声溢栈。所有 App / 驱动的运行期状态 (`app_data`) 一律走 `osmalloc`, 不允许使用 static / 全局数组承载 per-instance 数据。

`ksc_snake_ctx_t` (蛇游戏上下文)、`lfs_ctx_t` (littlefs 缓冲)、`term_ctx_t` 等都遵循这一约定。

---

## KSCdraw 图形引擎 (`inc/KSCdraw.h`)

### `k_draw_device` 抽象

```c
typedef struct k_draw_device {
    void*       data;            /* 硬件 / 驱动上下文 */
    SCR_INIT    init;
    SCR_SETCANVAS      setcanvas;
    SCR_SETCOLORPIXELS setcolorpixels;
    SCR_WINDOW_SETCANVAS setwindows;
    SCR_WINDOW_SETPIXELS setpixels;
} k_draw_device;
```

`screen_init / screen_setcanvas / screen_setcolorpixels` 提供 SPI 透明接口; `setwindows/setpixels` 是带 `KSC_window` 裁剪的窗口级接口。`k_draw_device_init()` / `k_draw_device_find(name)` 管理设备表。

### `KSC_window`

```c
typedef struct KSC_window {
    ksc_obj_t* objbuf;    /* 对象缓冲区 (NULL 即无) */
    KSCCOLOR   bk;        /* 背景色 */
    uintxy    width, height;
    uintxy    ssx, ssy;  /* 屏幕左上角坐标 */
    uint8_t   Mode;
    uint8_t   objnum;
} KSC_window;
```

### `ksc_obj_t` 与 draw_table

```c
typedef struct ksc_obj_t {
    void*     data;       /* 像素 / 字符串指针 */
    KSCCOLOR  colorck;
    ku8       width, height;
    ku8       sdx, sdy;   /* 屏幕坐标 */
    ku8       d_and_r;   /* 高 5 位: 私有标志 (反白/可见...), 低 3 位: 保留 */
    ku8       _type;      /* [高 4 位: 用户私有] [低 4 位: draw_table 索引] */
} ksc_obj_t;
```

- 低 4 位 `_type & _type_mask` 是 `draw_table` 的索引, 默认映射:
  ```
  _fillbox=0 _box=1 _line=2 _string=3 _image=4 _imagebig=5 _ibin=6
  _circle=7 _fillcircle=8 _arc=9 _roundrect=10 _fillroundrect=11 _char=12
  ```
- `kobjdraw_init(dev)` 初始化默认 draw_table; `ksc_set_draw_func(idx, fn)` / `ksc_get_draw_func(idx)` 可扩展自定义 `draw_fn(dev, win, obj)`。
- 高 4 位由用户自由使用 (如标识"被选中" / "动画帧")。

### 基本绘图 API

| API | 说明 |
|-----|------|
| `ksetpixel(dev,win,c,x,y)` | 单像素 |
| `kline(dev,win,c,x1,y1,x2,y2)` | 任意方向直线 (Bresenham) |
| `kbox / kfillbox` | 矩形边框 / 填充矩形 |
| `kcircle / kfillcircle` | 圆 / 填充圆 (`__DRAW_CIRCLE__`) |
| `karc(dev,win,c,x0,y0,r,Anglediraction)` | 1/4 圆弧, 方向位 `0x01/0x02/0x04/0x08` 命右上/左上/右下/左下 |
| `kroundrect / kfillroundrect` | 圆角矩形 / 填充圆角矩形 |
| `kchar(dev,win,ch,x,y,ck,cb)` | 单 ASCII 字符 |
| `kstring(dev,win,str,x,y,ck,cb)` | ASCII 字符串 |
| `kstringchinese(...)` | 中文字符串 (`__USE_CHINESE__`) |
| `kdrawimage(dev,win,img,x,y,w,h)` | 16-bit RGB565 像素图 |
| `kdrawimagebig(...,scale)` | 缩放图 |
| `kimagebin(dev,win,img,x,y,w,h,ck,cb)` | 1-bit 二值图 |
| `kscreendraw(dev,win)` | 渲染整窗口的 objbuf |
| `kobjdraw / kobjsdraw(...,num)` | 单个 / 多个对象 |

### 颜色

`KSCCOLOR = uint16_t`, RGB565。本配置 `__LITTLE_END_COLOR__ == 1` (字节序与屏一致):

| 宏 | 值 |
|----|-----|
| `rred` | `0x00F1` |
| `ggreen` | `0xE007` |
| `bblue` | `0x1F00` |
| `bblack` | `0x0000` |
| `wwhite` | `0xFFFF` |
| `yyellow` | `0xE0FF` |

也可用标准 RGB565 常量 `0xF800`(红) / `0x07E0`(绿) / `0x001F`(蓝)。

---

## 应用清单

`apps/` 下每个 `.c` 调用一次 `REGISTER_APP_EX` 注册为一个全局可 `appget` 的实例。

| 注册名 | app_dep | 平台 | 说明 | 源文件 |
|--------|---------|------|------|--------|
| `gpio_port` | — | STM32 | 全局引脚号直操寄存器 (CR/BSRR/IDR/ODR), 内含 RCC 懒初始化 | `gpio_port.c` |
| `uart_serial` | `gpio_port` | PC+STM32 | 统一 USART1/2/3, 中断 RX 环形缓冲 + 回调; mode=`(inst<<4)\|op` | `uart_serial.c` |
| `tim_clock` | — | PC+STM32 | TIM1-4 周期 / 单次; 回调注入 (PC: 后台线程) | `tim_clock.c` |
| `button16` | `gpio_port` | PC+STM32 | 4×4 矩阵键盘扫描; raw 位图 + 事件队列 (PRESS/RELEASE/HOLD/LONG/DBLCLICK) | `button16.c` |
| `super_spi` | `gpio_port` | STM32 | 统一 SPI1+SPI2 主控, 含 CS/DC/R1/R2 逻辑引脚 + DMA; `SSPI_MODE(i,d,op)` 编码 mode, `SSPI_XFER_INST(i)` 双工 | `super_spi.c` |
| `KSCGUI` | `super_spi` | PC+STM32 | GUI Tile 合成器 (16 槽 + Z 序) + ST7789 驱动 (PC: EasyX), ~30 个 appcmd 命令 | `kscgui.c` (详见 `docs/KSCGUI_API.md`) |
| `list` | `KSCGUI` | PC+STM32 | GUI 列表 widget, 256B 字符串池 + 碎片管理 + 5 种选中样式; 内置键盘控制 | `list.c` |
| `snake` | `KSCGUI` + `button16` | PC+STM32 | Snake 游戏, 全中断驱动 (TIM4@250ms), 对象增量渲染 | `snake.c` |
| `w25qxx_base` | `super_spi` | PC+STM32 | W25Q64 SPI NOR Flash: id/sr/uid/read/fast/write/erase/ce ⚠️ (PC: 文件模拟) | `w25qxx_base.c` |
| `littlefs` | `w25qxx_base` | PC+STM32 | littlefs 文件系统: 一次性 (`writenew`/`append`/`cat`/`ls`/`rm`/`mkdir`/`mv`/`stat`) + 持久 fd (`open`/`close`/`fread`/`fwrite`/`fseek`) | `littlefs_fs.c` |
| `terminal` | — | PC+STM32 | 字符串路由分发器: UART RX → 攒行 → 按 `appname subcmd -x v` 路由到 `appget` 目标; 内建 `help`/`echo` | `terminal.c` |
| `open` | `littlefs` | PC+STM32 | 按扩展名路由文件打开 (`.txt` → uart, `.bmp` → gui) | `open.c` |
| `transfer` | `uart_serial` + `littlefs` | STM32 | XMODEM-128 文件上传到 littlefs | `transfer.c` |

> **⚠️ 警告：`w25qxx_base` 的 `write` / `erase` / `ce` 是破坏性 appcmd**，会改写/擦除
> W25Q64 原始数据，**直接摧毁 littlefs 文件系统**（相当于删掉 `.data/flash.bin`）。
> littlefs 通过二进制接口（`appwrite` mode 1/3/5）使用 flash，**正常运行和测试都不需要
> 这些命令**。仅开发者做底层调试时使用，执行前请三思。
> 非破坏的只读命令：`id`（读 JEDEC ID）/ `sr`（读状态寄存器）。

跨 App 共享类型 (常量 / 结构) 定义在 `apps/app_config.h`:
- `SSPI_MODE(spi_inst, dev_id, op)` / `SSPI_XFER_INST(i)` — super_spi mode 编码宏
- `spi_xfer_t` — 全双工收发结构
- `tile_h_t` / `tile_info_t` — KSCGUI tile 句柄 (uint8 4-bit 生成 + 4-bit 槽位)
- `list_pos_t` / `list_colors_t` / `LIST_STYLE_*` — list widget
- `ctrl_keymap_t` / `ctrl_event_cb_t` / `CTRL_EVENT_CONFIRM` / `CTRL_EVENT_QUIT` — list 键盘控制

---

## 扩展指南: 编写自己的 App

### 最小模板

```c
/* apps/myapp.c */
#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>

#if __USE_STM32__
#include "stm32f1xx.h"

typedef struct {
    int counter;
    /* 所有 per-instance 状态放这里, 而不是 static */
} my_ctx_t;

static int my_open(app_t* app) {
    my_ctx_t* c = (my_ctx_t*)osmalloc(sizeof(*c));
    if (!c) return -1;
    c->counter = 0;
    app->app_data = c;
    return 0;
}

static int my_close(app_t* app) {
    if (app->app_data) {
        osfree(app->app_data);
        app->app_data = NULL;
    }
    return 0;
}

static int my_read(app_t* app, void* data, uint32_t count, uint32_t mode) {
    my_ctx_t* c = (my_ctx_t*)app->app_data;
    if (!c) return -1;
    (void)data; (void)count; (void)mode;
    return 0;
}

static int my_write(app_t* app, void* data, uint32_t count, uint32_t mode) {
    (void)app; (void)data; (void)count; (void)mode;
    return 0;
}

static int my_cmd(app_t* app, const char* cmdname, const char** argv) {
    if (strcmp(cmdname, "inc") == 0) {
        my_ctx_t* c = (my_ctx_t*)app->app_data;
        return c ? ++c->counter : -1;
    }
    if (strcmp(cmdname, "get") == 0) {
        my_ctx_t* c = (my_ctx_t*)app->app_data;
        return c ? c->counter : -1;
    }
    return -1;
}

static const papp_ops_t my_ops = {
    .open = my_open, .close = my_close,
    .read = my_read, .write = my_write, .cmd = my_cmd,
};

REGISTER_APP_EX("myapp", "0", NULL, &my_ops,
    "Demo app");
#endif
```

> `dep_str` 字段是遗留的 pdrv 依赖, 一律填 `"0"`; App 间依赖用第 3 参 `app_dep_str`。

### app_dep 字符串

```
"1\0gpio_port"                       → 1 个依赖, 即 gpio_port
"2\0KSCGUI\0button16"                 → 2 个依赖
"4\0a\0b\0c\0d"                      → 4 个依赖 (上限)
```

最多 4 个, 填入 `app0..app3`。被依赖的 App 必须已 `REGISTER_APP`; 否则 `appget` 失败。

### appcmd 解析约束

- 26 个 flag 槽 (`a..z`), 不够时改用 `appwrite` 二进制路径。
- 命令名是第一个空格前的 token, 不可以是 flag。
- 命令行总长 ≤ 128B (`APPCMD_LINE_MAX`), 超出截断。
- 优先用 `appcmd_argv` 跳过解析 (尤其在中断上下文或要传原始 argv 时)。

### ⚠️ 易错提醒: 所有参数必须用 `-x value` 格式

**错误写法（裸值被丢弃）**:
```
littlefs ls /           # ← / 被丢弃，实际 ls 了 CWD
littlefs cat /test.txt  # ← 被丢弃，不起作用
```

**正确写法（用 -x flag）**:
```
littlefs ls -p /
littlefs cat -p /test.txt
```

常见命令的 flag 约定:

| flag | 含义 | 示例 |
|------|------|------|
| `-p` | 路径 | `ls -p /`, `cat -p /f`, `open -p /f` |
| `-d` | 数据 (字符串) | `fwrite -d hello`, `echo -d "hello world"` |
| `-f` | 打开标志 (hex) | `open -p /f -f 0x502` |
| `-n` | 长度/数量 | `fread -n 64` |

### 引用计数 / open-close 注意

- `appget` 多次返回同一 `app_t*`, `get_refs` 累加; `appfree` 减。
- **`appclose` 不做引用计数** —— 多块代码共享同一 App 时, 一者 close 会清掉 `APP_STATE_OPENED` 并销毁 `app_data`, 另一者立刻失效。协作约定: 共享者各自持有 `appopen` 的状态, 只在能确定无其它使用方时才 `appclose`。
- 给 idle-loop 用的 App (terminal 等长驻型) 不要 close。

### 链接器提醒

`REGISTER_APP` 文件必须加入**目标可执行文件**的 `target_sources(${CMAKE_PROJECT_NAME} PRIVATE ...)`，不应进 `add_library(...)`。否则 orphan section 会被 (`--gc-sections` + LTO) 丢弃, `app_table` 段为空。参见 `apps/CMakeLists.txt`。

---

## 第三方依赖

下表各组件只为"特定平台 / 特定 App"提供一种实现，并非"框架之必需"。开发者可以替换、删除或用其它等价库替代——只需把对应的 App 模块从 `target_sources` 列表中去掉（或换写一份同名 `REGISTER_APP` 即可），框架核心不会受影响。

| 组件 | 路径 | 取代的具体实现 | 可替换为 |
|------|------|---------------|----------|
| easyx (仅 MinGW) | `third_party/easyx/` (libeasyx.a + graphics.h) | PC 平台 `k_draw_device` 屏呈现 | raylib / 自绘后端 |
| SDL2 (系统包) | Linux: `libsdl2-dev` | Linux 平台 `k_draw_device` 屏呈现 (RGB565 帧缓冲批量 present) | raylib / 直写 framebuffer |
| littlefs | `third_party/littlefs/` | `littlefs` App 的 FS 实现 | FatFS / 自实现 FS |
| tjpgd3 | `third_party/tjpgd3/` | JPEG 解码支持 | stb_image / libjpeg-turbo |
| STM32 CMSIS + 启动 | `third_party/stm32/` (CMSIS 头 + 启动汇编 + 链接脚本 + 系统源) | 寄存器宏定义、中断向量表、newlib 系统调用桩 | 完整 CubeMX HAL / LL 库 / 裸 CMSIS |

> KSCOS 框架核心 (`src/app.c`、`src/KSCOSsystem.c`、`src/KSCdraw.c`) 仅依赖 libc 与 CMSIS (`stm32f1xx.h`) — 所有外设操作在 App 内部直接读写寄存器，不依赖 HAL 中间件。

---

## 文档

- **`docs/api/`** — 完整 API 契约文档 (`app_framework.md` / `appcmd.md` / `system.md` / `kscdraw.md` / `apps.md`), 见 `examples/api/README.md` 文档地图。

---

## 许可证

MIT License, © 2026 OneSleepWorm (见 `LICENSE`)。
`third_party/` 下各组件遵循其各自许可。