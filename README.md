# 项目协议说明
1. 本项目 **src/ 目录下所有自研代码：MIT License（见根目录LICENSE）**
2. third_party/stm32/ 内 STM32 HAL库源码：版权归STMicroelectronics，遵循 **BSD 3-Clause License**，协议文件位于 third_party/stm32/LICENSE
3. 分发本项目时，第三方HAL库需保留原有BSD协议与版权信息。

# KSCOS — 轻量级嵌入式系统框架

KSCOS 是一个面向资源受限微控制器的轻量级嵌入式框架。它从最初的跨平台绘图库（KSCdraw）演进而来，现已集成了图形绘制、任务调度器、输入抽象、设备驱动抽象、应用生命周期管理、命令系统和文件系统支持——全部在极低的 RAM 开销下运行。

> **重要：** KSCOS 提供的是**框架和 HAL 抽象接口**（函数指针），具体的硬件驱动实现（屏幕、按键矩阵、Flash 等）仍需用户根据目标平台自行完成。它不是一个开箱即用的完整操作系统，而是一个帮助你组织嵌入式代码的软件框架。

## 功能特点

### 图形子系统（原 KSCdraw）

- **基本图形**：点、线、矩形、圆形、圆角矩形、圆弧、填充图形
- **图像显示**：直接绘制、二值化绘制、缩放绘制、多色深解码（1/2/4-bit → RGB565）
- **文本渲染**：ASCII 字符（7×7 / 8×16），可选中文支持（通过 Flash 字库，需 `__USE_CHINESE__`）
- **内置图标**：12 个 16×16 RGB565 应用图标 + 1 个 40×40 标志图案

### 对象与渲染系统

- **对象系统**：统一 `ksc_obj_t`（12 字节）描述 11 种可绘制元素，支持批量绘制
- **脏矩形增量渲染**：自动追踪变更区域、合并重叠脏矩形、仅重绘受影响区域——减少 SPI 总线传输量

### 系统层

- **系统初始化**：`KSCOSsystem_Init()` 封装 HAL 初始化；`KSCOSSystemClock_Init()` 提供 3 档时钟预设（8MHz / 72MHz / 168MHz）
- **全局错误处理**：`KSCOS_Error_Handler()` / `KSCOS_default_Error_Handler()` 可被各子系统调用
- **系统时钟变量**：`KSCOSsystem_Clock`（volatile uint32_t）供定时器子系统使用

### 任务调度器

- **周期性任务**：`clock_task_t` 提供 init → run → callback（按周期触发）→ stop 完整生命周期
- **双平台实现**：ARM 基于 TIM2 硬件定时器 + 中断回调；PC 基于 pthread + Sleep 循环
- **工厂创建**：`clock_task_create(init, callback, user_data, cycle_ms)` 一行创建

### 输入抽象

- **设备无关输入队列**：4 槽环形缓冲区，`input_t` 含 32-bit data + 32-bit device_id
- **可插拔驱动接口**：`input_device` 结构体含 init / create / deinit 函数指针
- **按键驱动**：ARM — 4×4 矩阵扫描（16 键编码）；PC — EasyX 键盘模拟

### 应用框架

- **自注册机制**：`REGISTER_APP(name, init, update, deinit)` 利用 GCC linker section 自动收集应用
- **生命周期管理**：`ksc_app_init()` 按名称哈希查找并初始化，自动分配 `KSC_window` + `k_draw_device`
- **应用枚举**：`ksc_app_list()` 列出所有已注册应用

### 命令系统

- **自注册命令**：`REGISTER_CMD(name, handler)` 同 linker section 模式自动收集
- **运行时查找**：`run_command(name, ...)` 遍历命令表执行匹配项

### 存储与文件系统

- **SPI Flash 驱动**：完整 W25Q64 指令集（页编程、扇区擦除、读数据等）；PC 端支持文件模拟（`.data/w25q64_sim.bin`），无需硬件即可开发
- **嵌入式文件系统**：集成 littlefs v2.0.11（vendored），已配置适配 W25Q64（256B 读写 / 4096B 块 / 500 次磨损均衡）

### 跨平台 HAL

- **三平台编译**：PC（EasyX）/ STM32F10x（ARM）/ ESP32
- **统一设备抽象**：`k_draw_device` 函数指针表（init / setcanvas / setcolorpixels / setwindows），`kscreenmount()` 自动挂载
- **预处理器驱动**：`inc/KSCconfig.h` 中通过 `__USE_PC__` / `__USE_STM32__` / `__USE_ESP32__` 选择平台，编译期剔除未用代码

### 通信

- **串口/UART 抽象**：`kprintf` / `kfgetc` / `serial_sendbyte` 等——ARM 走 UART 外设，PC 映射到 stdio

---

## 快速开始

### 0. 核心类型

```c
KSCCOLOR       // RGB565 颜色 (uint16_t)
uintxy         // 坐标 (uint16_t)
KSC_window     // 屏幕描述结构体
k_draw_device  // 设备驱动接口（自动挂载）
clock_task_t   // 周期任务
input_t        // 输入事件（data + device_id）
```

### 1. 系统初始化（ARM 平台）

```c
#include "inc/master.h"

// 必须先初始化系统层（PC 平台可跳过）
KSCOSsystem_Init();
KSCOSSystemClock_Init(KSCOS_NORMAL_CLOCK);  // 72MHz
```

### 2. 挂载设备并初始化屏幕

```c
// kscreenmount() 自动创建 k_draw_device 并调用 init()
k_draw_device* dev = kscreenmount();

// 栈分配屏幕
KSC_window screen = {
    .ssx = 0, .ssy = 0,
    .width = 240, .height = 160,
    .bk = wwhite,
};
```

### 3. 绘制图形与文本

```c
kfull(dev, &screen, bblue, 0, 0, 240, 160);            // 填充矩形
kroundrect(dev, &screen, rred, 80, 50, 80, 60, 8);     // 圆角矩形
kfillcircle(dev, &screen, ggreen, 120, 80, 30);         // 填充圆形
kstring(dev, &screen, "Hello KSCOS", 10, 0, rred, wwhite);  // 文本
```

### 4. 创建周期任务

```c
void my_timer_callback(clock_task_t* task) {
    // 每个周期执行一次（例如 100ms）
    // 在这里轮询按键、更新状态...
}

clock_task_t* task = clock_task_create(NULL, my_timer_callback, NULL, 100);
task->run(task);   // 启动定时器
```

### 5. 处理输入

```c
input_t evt;
if (input_get(PC_KEY_DEVICE_ID) == 0) {   // 0 = 有数据
    // evt.data 中为按键编码，evt.input_id 为设备 ID
    char ch = '0' + (evt.data & 0x0F);
    // 更新屏幕...
}
```

---

## 配置选项

在 `inc/KSCconfig.h` 中通过宏配置：

| 宏 | 作用 | 默认值 |
|---|---|---|
| `__USE_PC__` / `__USE_STM32__` / `__USE_ESP32__` | 平台选择（同一时间只应启用一个） | STM32=1 |
| `__USE_AUTO__` | 由构建系统自动检测平台（CMake 定义） | — |
| `__USE_LCD__` | 启用绘图库 | 1 |
| `__USE_CLOCK_TASK__` | 启用定时器/任务调度器 | 1 |
| `__USE_CHINESE__` | 启用中文字符渲染 | 0 |
| `__USE_FLASH__` | 启用 SPI Flash (W25Q64) | 0 |
| `__USE_LITTLEFS__` | 启用 littlefs 文件系统 | 0 |
| `__USE_KEY__` | 启用按键输入 | 1 |
| `__USE_UART__` | 启用串口通信 | 0 |
| `__USE_TEXT__` | 启用文本渲染 | 1 |
| `INPUT_QUEUE_SIZE` | 输入队列槽数 | 4 |
| `__BUTTON_SIMU__` | PC 模拟按键模式 | 1 |
| `__LITTLE_END_COLOR__` | RGB565 字节序（1=小端，0=大端） | 1 |
| `SYSTEMFONT` | ASCII 字体宽度（7 或 8） | 8 |
| `COLORBIT` / `COLORBYTE` | 颜色位深（默认 2 字节 RGB565） | 2 |
| `TFTx` / `TFTy` | 屏幕分辨率 | 240×160 |
| `_STATICBUF_SIZE` | 静态绘制缓冲区大小（字节） | 512 |
| `MAX_INPUT_SIZE` | 单次输入数据最大长度 | 255 |

---

## 目前适配

### 平台支持

| 平台 | 状态 | 说明 |
|---|---|---|
| **PC（EasyX）** | ✓ 可用 | 开发/调试主力平台；键盘模拟输入、文件模拟 Flash |
| **STM32F10x（ARM）** | ✓ 可用 | Keil/MDK 编译；需用户实现 HAL 接口 |
| **ESP32** | △ 框架就绪 | 头文件已有平台分支，待完整验证 |

### 显示屏支持

- **ST7735** / **ST7789** — 已内置初始化命令
- **其他 TFT 屏幕** — 修改 `KSCdisplay.h` 中的 `init_cmds` 即可适配
- 如需其他驱动库支持，可联系作者 OneSleepWorm(一只瞌睡虫) 或提交 Pull Request

> **提醒：** HAL 函数 `screen_init` / `screen_setcanvas` / `screen_setcolorpixels` 需用户根据目标硬件自行实现。PC 平台的 EasyX 实现位于 `src/KSCdisplay.c`，可作为参考。

---

## 目录结构

```
KSCOS/
├── makefile                  # PC 构建（TDM-GCC-64 + EasyX）
├── CMakeLists.txt            # CMake 构建（支持 MinGW Makefiles / Ninja）
├── CMakePresets.json         # CMake 预设（Debug / Release）
├── PreLoad.cmake             # Windows 下强制 MinGW Makefiles
├── master.c                  # 入口 / 演示程序（键盘输入 + 对象绘制）
├── inc/                      # 头文件（17 个）
│   ├── KSCconfig.h           # 全局配置宏（平台选择、功能开关、颜色、分辨率）
│   ├── KSCOSsystem.h         # 系统初始化 + 时钟配置 + 错误处理
│   ├── KSCdraw.h             # 核心绘图 API + k_draw_device + KSC_window + ksc_obj_t
│   ├── KSCdisplay.h          # 显示驱动 HAL 接口（screen_init / setcanvas / setcolorpixels）
│   ├── KSCfont.h             # 字体系统接口（KSC_Font1 / KSC_FontChinese）
│   ├── KSCimg.h              # 图像数据定义 + kloadimage
│   ├── clocktask.h           # 定时器 / 任务调度器
│   ├── input.h               # 通用输入队列系统（input_t / input_device）
│   ├── key.h                 # 按键输入抽象（k_key_device + 16 键编码）
│   ├── application.h         # 应用程序框架（REGISTER_APP + linker section）
│   ├── cmd.h                 # 命令系统（REGISTER_CMD + linker section）
│   ├── TFTDriver.h           # TFT SPI 底层驱动接口（ARM 平台）
│   ├── W25Q64.h              # SPI Flash 驱动（W25Q64，支持 PC 文件模拟）
│   ├── W25Q64_Ins.h          # Flash 指令集定义
│   ├── Serial.h              # 串口 I/O 抽象（kprintf / kfgetc）
│   ├── UTF8_FlashN.h         # UTF-8 解析 + Flash 字库地址映射
│   └── master.h              # 主程序头文件聚合
├── src/                      # 源文件（8 个）
│   ├── KSCdraw.c             # 核心绘图 + 对象系统 + 脏矩形管理（约 700 行）
│   ├── KSCdisplay.c          # 显示 HAL（PC EasyX / ARM TFT）
│   ├── KSCfont.c             # 字模数据 + 字体函数指针实现
│   ├── KSCimg.c              # 图像数据（12 应用图标 + OneSleepWorm 标志）
│   ├── KSCOSsystem.c         # 系统初始化实现（STM32 时钟树 / 错误处理）
│   ├── clocktask.c           # 任务调度器实现（TIM2 中断 / pthread）
│   ├── input.c               # 输入队列实现（环形缓冲区）
│   └── key.c                 # 按键驱动实现（4×4 矩阵扫描 / PC 键盘模拟）
├── third_party/              # 第三方依赖
│   ├── easyx/                # EasyX 图形库（include + libeasyx.a）
│   ├── littlefs/             # littlefs v2.0.11 嵌入式文件系统（vendored）
│   └── stm32/                # STM32 HAL 外设驱动（SPI, TIM；BSD 3-Clause）
├── examples/                 # 使用示例
│   ├── obj/                  # basicdraw.c / inputdraw.c / timertask.c
│   └── Dependence/           # 示例 CMake 配置
├── .data/                    # 测试资源（w25q64_sim.bin，8 MB Flash 模拟）
├── LICENSE                   # MIT 许可证
└── README.md
```

---

## API 参考

### 系统初始化

| 函数 | 说明 |
|---|---|
| `KSCOSsystem_Init()` | 初始化硬件抽象层（ARM：调用 HAL_Init） |
| `KSCOSSystemClock_Init(type)` | 配置系统时钟，type: `KSCOS_LOW_CLOCK` / `KSCOS_NORMAL_CLOCK` / `KSCOS_HIGH_CLOCK` |
| `KSCOS_Error_Handler()` | 全局错误处理入口（用户可重写） |
| `KSCOS_default_Error_Handler(data)` | 默认错误处理（含调试信息） |

### 任务调度器

| 函数 | 说明 |
|---|---|
| `clock_task_create(init, callback, data, cycle)` | 创建周期性任务（工厂函数） |
| `task->init(task)` | 初始化任务（分配资源） |
| `task->run(task)` | 启动定时器，开始周期性回调 |
| `task->stop(task)` | 停止定时器，释放资源 |
| `clock_default_task` | 全局默认任务单例（可直接赋值回调使用） |

### 设备 & 屏幕管理

| 函数 | 说明 |
|---|---|
| `kscreenmount()` | 自动创建并挂载设备驱动，返回 `k_draw_device*` |
| `k_draw_device_init()` | 手动初始化全局设备 |
| `k_draw_device_find(name)` | 按名称查找设备 |
| `kscreeninit(dev, ssx, ssy, w, h, bk)` | 动态分配屏幕，返回 `KSC_window*` |
| `kscreenfree(dev, screen)` | 释放动态分配的屏幕 |
| `kscreenclear(dev, screen)` | 用背景色清屏 |

### 基本图形

| 函数 | 说明 |
|---|---|
| `ksetpixel(dev, screen, color, x, y)` | 设置单像素 |
| `kfull(dev, screen, color, x1, y1, w, h)` | 填充矩形区域（直线段/矩形边框的底层实现） |
| `kline(dev, screen, color, x1, y1, x2, y2)` | 任意方向线段（水平/垂直走快速路径） |
| `kbox(dev, screen, color, x1, y1, w, h)` | 矩形边框 |
| `kfillbox(dev, screen, color, x1, y1, w, h)` | 填充矩形 |
| `kcircle(dev, screen, color, x0, y0, r)` | 圆形边框 |
| `kfillcircle(dev, screen, color, x0, y0, r)` | 填充圆形（扫描线算法） |
| `karc(dev, screen, color, x0, y0, r, dir)` | 圆弧（按象限位掩码选择） |
| `kroundrect(dev, screen, color, x1, y1, w, h, r)` | 圆角矩形边框 |
| `kfillroundrect(dev, screen, color, x1, y1, w, h, r)` | 填充圆角矩形 |

### 图像 & 文本

| 函数 | 说明 |
|---|---|
| `kdrawimage(dev, screen, img, x, y, w, h)` | 绘制 RGB565 图像 |
| `kdrawimagebig(dev, screen, img, x, y, w, h, scale)` | 缩放绘制图像 |
| `kimagebin(dev, screen, img, x, y, w, h, ck, cbk)` | 绘制二值化图像（1-bit → RGB565） |
| `kchar(dev, screen, ch, x, y, ck, cbk)` | 绘制单个 ASCII 字符 |
| `kstring(dev, screen, str, x, y, ck, cbk)` | 绘制 ASCII 字符串 |
| `kstringchinese(dev, screen, str, x, y, c1, c2)` | 绘制中文字符串（需 `__USE_CHINESE__`） |
| `kloadimage(name)` | 按名称加载 Systemimg 中的图像，返回 `Img_File` |

内置图标（`KSCimg.c`）：`Wechat`, `QQ`, `Setting`, `Video`, `Photo`, `Tiktok`, `Qwen`, `Note`, `Game`, `Clock`, `Book`, `Alipay`（16×16 RGB565）

### 对象系统

对象类型常量：`_box`, `_circle`, `_string`, `_image`, `_imagebig`, `_roundrect`, `_fillroundrect`, `_fillbox`, `_fillcircle`, `_line`, `_char`

| 函数 | 说明 |
|---|---|
| `kobjdraw(dev, screen, obj)` | 绘制单个 `ksc_obj_t`（按 `_type` 分发） |
| `kobjsdraw(dev, screen, obj_array, num)` | 批量绘制（跳过非活跃/隐藏对象） |
| `kscreendraw(dev, screen)` | 全屏重绘（背景 + 所有对象） |

### 输入系统

| 函数 | 说明 |
|---|---|
| `input_get(input_id)` | 从输入队列取出事件（返回 0 表示成功取到数据） |
| `input_add(input)` | 向输入队列添加事件 |
| `key_default_device` | 全局按键输入设备单例（`input_device` 类型） |

### 应用框架

| 函数/宏 | 说明 |
|---|---|
| `REGISTER_APP(name, init, update, deinit)` | 注册应用到 linker section |
| `ksc_app_init(name, argv)` | 按名称实例化并初始化应用 |
| `ksc_app_list()` | 枚举所有已注册应用 |

### 命令系统

| 函数/宏 | 说明 |
|---|---|
| `REGISTER_CMD(name, handler)` | 注册命令到 linker section |
| `run_command(name, ...)` | 按名称查找并执行命令 |
| `list_cmds()` | 列出所有已注册命令 |
| `show_dir()` | 显示当前目录 |

### 内部工具函数

| 函数 | 说明 |
|---|---|
| `imgchange(src, dst, len, bit, colortable)` | 多色深解码（1/2/4-bit → RGB565），32 位宽写优化 |
| `memset_16(buf, size, pattern)` | RGB565 批量填充（指数复制） |
| `app_name_hash(name)` | 应用名称字符串哈希（用于快速查找） |
| `utf8getulen(str)` | 获取 UTF-8 字符字节长度 |
| `utf8get(str, &unicode)` | 提取 Unicode 码点 |

---

## 架构概览

```
┌─────────────────────────────────────────────┐
│            应用层 (Application)              │
│    ksc_app (REGISTER_APP)                    │
│         │  ┌──────────────┐                  │
│         └──┤  命令系统     │                  │
│            │  REGISTER_CMD │                  │
│            └──────┬───────┘                  │
├───────────────────┼─────────────────────────┤
│          任务调度层 (clocktask)               │
│    clock_task_create / run / callback / stop │
│         │                                    │
├─────────┼────────────────────────────────────┤
│    输入层 (input)  │  绘图层 (KSCdraw)        │
│  input_t 队列      │  k_draw_device           │
│  input_device 驱动 │  KSC_window + ksc_obj_t  │
│                    │  对象标志位控制绘制         │
├────────────────────┼─────────────────────────┤
│              HAL 抽象层                        │
│  KSCdisplay   KSCOSsystem   W25Q64   Serial  │
│  (屏幕驱动)    (时钟/初始化)  (Flash)  (串口)   │
├──────────────────────────────────────────────┤
│              平台 (Platform)                  │
│  STM32 HAL  │  EasyX (PC)  │  ESP-IDF       │
└──────────────────────────────────────────────┘
```

**数据流示例（按键 → 屏幕）：**
```
key_scan() → input_add(&evt) → 输入队列
                                  ↓
main loop: input_get(KEY_ID) → 解析按键 → 修改 ksc_obj_t 属性
                                  ↓
kscreendraw() → kobjsdraw() → kobjdraw() → screen_setcolorpixels()
```

**HAL 责任边界：** KSCOS 定义接口（函数指针），用户/平台负责实现。例如 `k_draw_device.setcolorpixels` 在 PC 上由 EasyX `PutPixels` 实现，在 ARM 上由 `TFT_Setcolors`（SPI/DMA）实现。

---

## 命名约定

项目中多种命名前缀共存，各有含义：

| 前缀 | 范围 | 示例 |
|---|---|---|
| `k_` | 公开绘图 API | `ksetpixel`, `kline`, `kstring`, `kfillcircle` |
| `ksc_` | 核心库对象/函数 | `ksc_obj_t`, `ksc_app`, `kscreenmount`, `kobjsdraw` |
| `KSC` 大写 | 类型、枚举、常量 | `KSC_window`, `KSCCOLOR`, `KSC_Img`, `KSC_Font1` |
| `KSCOS` 大写 | OS/系统层 | `KSCOSsystem_Init`, `KSCOS_Error_Handler`, `KSCOS_NORMAL_CLOCK` |
| 无前缀 | 平台 HAL / 驱动函数 | `screen_init`, `key_scan`, `TFT_Init`, `input_get` |

---

## 构建

### GNU Make（PC / TDM-GCC-64 + EasyX）

```bash
make             # 编译 → build/mk/KSCOS.exe
make run         # 运行
make clean       # 清理
make DEBUG=1     # 调试构建（-O0 -g）
make reboot      # clean → build → run
make reboot-debug # clean → debug build → run
```

### CMake（支持 MinGW Makefiles / Ninja）

```bash
# 使用预设配置
cmake --preset default          # MinGW Makefiles, Release
cmake --build build

# 或手动配置
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Ninja 构建
cmake --preset ninja
cmake --build build_ninja
```

### VSCode

`Ctrl+Shift+B` 提供预配置的构建任务：`make reboot` / `make reboot-debug` / `cmake reboot` / `cmake reboot-debug` / `build clean` / `run KSCOS`。

---

## 移植指南

将 KSCOS 移植到新平台需要以下步骤：

1. **设置平台宏** — 在 `KSCconfig.h` 中将目标平台宏设为 `1`，其余平台设为 `0`（或通过 CMake 定义 `__USE_AUTO__` 让构建系统决定）
2. **实现显示 HAL** — 实现 `KSCdisplay.h` 中的三个函数：
   - `screen_init()` — 初始化屏幕硬件
   - `screen_setcanvas(Gx, Gy, width, height)` — 设置绘图窗口
   - `screen_setcolorpixels(color_array, num)` — 批量写入像素颜色
3. **挂载设备** — `kscreenmount()` 会自动将 HAL 函数组装为 `k_draw_device` 并调用 `init()`。如需自定义设备名，使用 `k_draw_device_init()`
4. **实现 TFT 底层驱动** — 对于 ARM 平台，需实现 `TFTDriver.c` 中的 SPI/并行接口函数（参考已有 STM32F1 实现）
5. **配置系统时钟** — 实现 `KSCOSSystemClock_Init()` 的时钟预设，根据目标 MCU 的时钟树配置对应频率
6. **实现错误处理** — 重写 `KSCOS_Error_Handler()` 适配平台的异常上报方式（LED 闪烁、串口输出等）
7. **实现按键驱动**（可选，需 `__USE_KEY__`）— 实现 `key_init()` / `key_scan()`，参考 `key.c` 中 STM32 4×4 矩阵扫描实现；PC 平台已有 EasyX 键盘模拟
8. **实现 Flash 驱动**（可选，需 `__USE_FLASH__`）— 为 PC 平台提供文件模拟（参考 `.data/w25q64_sim.bin`），为真实硬件实现 SPI Flash 读写

---

## 许可证

MIT — 详见 `LICENSE` 文件
