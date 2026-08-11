# Apps API

> 实现: `KSCOS/apps/*.c` · 公共类型头: `KSCOS/apps/app_config.h`

本文件列出除 upload 之外所有已 `REGISTER_APP_EX` 的 App 的：
1. 注册信息（注册名、`app_dep`、平台、内部上下文大小）
2. `appcmd` 命令表
3. `appread` / `appwrite` 的 `mode` 编码

> 文件名通常等于 App 注册名（`gpio_port.c` → `"gpio_port"`），但少数有出入。
> 所有命令均通过 `appcmd` 入口；返回 `≥0` 成功，`<0` 错误，详见 [`appcmd.md`](appcmd.md)。
> 所有 App 的内部上下文均 `osmalloc` 于 `open`，由 `close` 释放；不可使用 static 全局承载 per-instance 状态。

## 总览

| 注册名 | app_dep | 平台 | 源文件 | 主要接口 |
|--------|---------|------|--------|---------|
| `system` | — | 双端 | `bsp/{stm32,pc}/system.c` | 固定地址内核服务; `appcmd(time/delay/idle/mem)` |
| `gpio_port` | — | STM32 | `bsp/stm32/gpio_port.c` | `appwrite` / `appread` + `appcmd(cfg/set/tog/rd)` |
| `uart_serial` | `gpio_port` | 双端 | `bsp/{stm32,pc}/uart_serial.c` | `appwrite`(TX) + `appread`(RX) + callback |
| `tim_clock` | — | 双端 | `bsp/{stm32,pc}/tim_clock.c` | `appwrite` + `appcmd(regcb/period/start/stop/rd)` |
| `button16` | `gpio_port` | 双端 | `bsp/{stm32,pc}/button16.c` | `appwrite`(init/start) + `appread`(raw/event) |
| `super_spi` | `gpio_port` | STM32 | `bsp/stm32/super_spi.c` | `appcmd(reg/setpin)` + `appwrite(SSPI_MODE)` |
| `KSCGUI` | `super_spi` | 双端 | `apps/kscgui.c` + `bsp/{stm32,pc}/gui_drv.c` | `appcmd`(~30 个命令) |
| `list` | `KSCGUI` | 双端 | `apps/list.c` | `appcmd`(init/add/select/...) + `appread` |
| `ctrl_list` | `list` + `button16` | STM32 | `apps/ctrl_list.c` | `appcmd(init/bind/start)` + callback |
| `snake` | `KSCGUI` + `button16` | 双端 | `apps/snake.c` | `appcmd(init)` |
| `w25qxx_base` | `super_spi` | 双端 | `bsp/{stm32,pc}/w25qxx_base.c` | `appcmd(id/read/write/erase/ce)` + `user_data` |
| `littlefs` | `w25qxx_base` | 双端 | `apps/littlefs_fs.c` | `appcmd(format/mount/writenew/open/...)` |
| `terminal` | — | 双端 | `apps/terminal.c` | `appwrite`(raw 行流) |
| `open` | `littlefs` | 双端 | `apps/open.c` | `appcmd` 转发到 littlefs/gpi_port |

> snake 还隐式依赖 `tim_clock`，由 `appget` 在内部抓取：(见 `snake.c`)。ctrl_list 同样。

---

## system

> 固定地址内核服务 app（内核服务即 app）。无平台差异的公开接口见 [`system.md`](system.md)。
> 注册：`bsp/{stm32,pc}/system.c`，`REGISTER_APP("system", "0", &system_ops, ...)`。

### appcmd 命令表

| 命令 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `time` | — | 1 | 输出当前 ms tick 到 `output_data` (uint32) |
| `delay` | `-t <ms>` | 1 | 阻塞延时 |
| `idle` | — | 1 | 空闲等待 (STM32: WFI; PC: Sleep(1)) |
| `mem` | — | 1 | 输出内存池统计到 `output_fn` (各档 used/peak/alloc/free/fail) |

> `malloc` / `free` 命令为保留命令名，实际分配走 `appwrite(SYSTEMAPP, &ptr, size, 0/1)` 二进制接口（快、无字符串解析）。

### appread / appwrite mode 表

| mode | 方向 | 说明 |
|------|------|------|
| `read mode 0` | 读 | `data` 出 `uint32` tick (等效 `sysgettime`) |
| `read mode 1` | 读 | `data` 出 `uint32` 系统时钟频率 |
| `write mode 0` | 写 | malloc: `data=&void*`，`count=size`，成功写回地址 |
| `write mode 1` | 写 | free: `data=&void*`，释放 `*data` |
| `write mode 2` | 写 | delay: `data=&uint32 ms` (等效 `sysdelay`) |
| `write mode 3` | 写 | idle (等效 `oswait_idle`) |

### 特殊约束

- **禁止 `appclose`**：`appclose(SYSTEMAPP)` 返回 -1。
- **`appopen` 幂等**：system 已在 `appget` 时自动 open，重复 open 返回 0。
- **固定地址**：不参与 osmalloc，地址由 `.system_zone` 段（STM32）/ 静态数组（PC）固定。

---

## gpio_port

> 全局引脚号直操寄存器，无前置依赖。

### 引脚编号

```
0-15   = GPIOA 0-15
16-31  = GPIOB 0-15
32-47  = GPIOC 0-15
48-63  = GPIOD 0-15 (预留)
```

`PORT_OF(pin) = pin >> 4`, `PIN_OF(pin) = pin & 0xF`。RCC 懒初始化（首次操作某端口时打开对应 RCC bit）。

### `appwrite` mode 表

`mode` 字节直接编码操作（不走 `inst<<4|op`）：

| mode | data | count | 操作 |
|------|------|-------|------|
| 0 | — | — | no-op |
| 1 | — | `(pin<<4) \| nibble` | CR 配置: nibble = `(CNF<<2) \| MODE` |
| 2 | `uint32_t* 0/1` | pin | BSRR 单引脚置/复位 |
| 3 | `uint32_t set32` | `reset32` | 批量 BSRR (bit0-31 = PA+PB) |
| 4 | — | pin | 翻转 pin |
| 5 | `uint32_t* 0/1/2` | pin | 上下拉: 0=浮空, 1=上拉, 2=下拉 |
| 6 | `uint32_t* 16bit` | `port_base 0/16/32` | 写整端 ODR |

### `appread` mode 表

| mode | data | count | 操作 |
|------|------|-------|------|
| 0 | — | — | no-op |
| 1 | `uint32_t* 16bit` | `port_base 0/16/32` | 读整端口 IDR |
| 2 | `uint32_t* 0/1` | pin | 读单 pin IDR |
| 3 | `uint32_t* 16bit` | `port_base 0/16/32` | 读整端口 ODR |

### `appcmd` 命令

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `cfg` | `-p <pin> -m <nibble>` | 0/-1 | 配置 CR: nibble=`(CNF<<2)\|MODE` (等价于 appwrite mode=1) |
| `set` | `-p <pin> -v <0/1>` | 0/-1 | 置/复位 (等价 appwrite mode=2) |
| `tog` | `-p <pin>` | 0/-1 | 翻转 pin (等价 appwrite mode=4) |
| `rd`  | `-p <pin>` | 1/-1 | 读单引脚电平到 `app->callback_data` (作为 `uint32_t*`) |

### 用法

```c
app_t* gpio = appget("gpio_port");
appopen(gpio);

appcmd(gpio, "cfg -p 4 -m 3");         /* PA4 推挽输出 50MHz: CNF=0,MODE=3 */
appcmd(gpio, "set -p 4 -v 1");         /* PA4 = HIGH */

uint32_t v;
app->callback_data = &v;
appcmd(gpio, "rd -p 4");                /* 读 PA4 电平 → v */
```

---

## uart_serial

> 统一 USART1/2/3, 中断 RX 环形缓冲 + callback。

### `appwrite` mode = `(inst << 4) | op`, inst ∈ {1,2,3}

| op | data | count | 说明 |
|----|------|-------|------|
| 1 | bytes | n | 轮询发送 n 字节 |
| 2 | — | 1/0 | 开/关 RXNE 中断 |
| 3 | `uint32_t* baud` | — | 设波特率（重算 BRR） |
| 4 | — | — | noop (初始化占位) |
| 5 | — | 1~3 | 设 ioctl 默认实例 |

### `appread` mode = `inst | flags`

- bits[3:0] = inst (1/2/3)
- bit6 = 阻塞读标记
- bit7 = 读 overflow 计数（`count>0` 自动清零）

| mode | 行为 |
|------|------|
| `inst` | 非阻塞读，无数据返回 0 |
| `inst \| 0x40` | 阻塞读，空转直到有数据 |
| `inst \| 0x80` | 读并在 `count>0` 时清零 overflow 计数 |

### 回调

```c
app->callback = on_rx;        /* RX 环空→非空时调用 (中断上下文) */
app->user_data = my_ctx;
```

### 没有 appcmd 命令

uart_serial 通过 `appcmd("open -i <inst>")` 在 `sys_init` 中被初始化，该 subcmd 来自内部硬编码（uart 的 `cmd` 仅识别 `open` 用于打开实例 + 设默认波特率），其它控制走 `appwrite`。

### 用法

```c
appwrite(u, "Hello\r\n", 7, 0x11);   /* USART1 轮询发送 */
appread(u, buf, 64, 1);              /* USART1 非阻塞读 */
appread(u, buf, 64, 0x41);           /* USART1 阻塞读 (0x40|1) */
```

---

## tim_clock

> TIM1-4 周期 / 单次，回调注入。

### `appwrite` mode = `(inst << 4) | op`, inst ∈ {1,2,3,4}

| op | 行为 |
|----|------|
| 0 | noop |
| 1 | 设 period：`app->user_data` 为 `uint32_t ms` 初值，启动定时更新 |
| 2 | 启动 / 停止切换 |

实际实现：内部 `inst_init` 期望 `app->user_data` 为毫秒周期指针（默认 1000ms）；周期 via `appcmd period` 改。

### `appcmd` 命令

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `regcb` | `-i <inst>` | 0/-1 | 注册 callback 到该 inst：`app->callback` + `app->user_data` 复制为 cb/ud |
| `period` | `-i <inst> -m <ms>` | 0/-1 | 重设 ARR = `10 * ms - 1` (psc = CLOCK/10000-1) |
| `start` | `-i <inst>` | 0/-1 | CEN=1, 使能 UIE |
| `stop`  | `-i <inst>` | 0/-1 | CEN=0, 关 UIE |
| `rd`    | `-i <inst>` | 1/-1 | 读当前周期（`(ARR+1)/10` ms）到 `callback_data` |

### 中断回调

`tim_irq_handler(int idx)` 在 IRQn 中查找 `tim_owners[idx]`，调 `ctx->cb[idx](ctx->ud[idx])`。
- `appget("tim_clock")` → `appopen` → `appcmd("regcb -i 1")` 注册回调。

### 用法

```c
app_t* t = appget("tim_clock");
appopen(t);
appcmd(t, "period -i 1 -m 250");
app->callback = my_tick_cb;
app->user_data = my_ctx;
appcmd(t, "regcb -i 1");
appcmd(t, "start -i 1");
```

---

## button16

> 4×4 矩阵键盘扫描。Row0-3 = GPIO 低 4 位（输出），Col0-3 = GPIO 高 4 位（输入上拉）。键号 = `row * 4 + col` (0-15)。

### `appwrite` mode (直接 = 整型 op, 不分 inst)

| op | data | count | 行为 |
|----|------|-------|------|
| 1 | NULL | 0 | 初始化 GPIO (推挽输出 + 上拉输入) |
| 2 | `uint32_t*` interval_ms | 1 | 启动定时器扫描 (依赖 tim_clock) |
| 4 | `uint32_t* 0/1` | 1 | 复杂模式开关 |
| 5 | `uint32_t[2]` `{hold_ticks, hold_gap}` | 1-2 | HOLD 触发参数 |

### `appread` mode

| mode | data | 返回 | 说明 |
|------|------|------|------|
| 1 | `uint32_t*` | 4 | `latest_keys` (16-bit raw 位图, bit n = 按键 n 按下) |
| 2 | `uint32_t*` | 4 | `interval_ms` |
| 3 | `uint32_t*` | 4 / 0 | 弹事件 (复杂模式), 无事件返回 0: bit31=1, bits7-4=键号, bits3-0=事件类型 |

事件类型：

| 值 | 事件 |
|----|------|
| 0 | PRESS |
| 1 | RELEASE |
| 2 | HOLD (超过 `hold_ticks` 个 tick) |
| 3 | LONG (> 1000ms) |
| 4 | DBLCLICK (400ms 内第二次 PRESS) |

### `appcmd` 命令

button16 的 `appcmd` 处理较简单，主要用于参数设置；常规驱动靠 `appwrite/appread`。`APPCMD_HAS(argv, 'e')` 在内部用于触发一次事件查询。详见源码 `button16.c`。

### 用法

```c
app_t* kpd = appget("button16");
appopen(kpd);
appwrite(kpd, NULL, 0, 1);              /* init GPIO */

uint32_t interval = 50;
appwrite(kpd, &interval, 1, 2);        /* start scan */

uint32_t enable_complex = 1;
appwrite(kpd, &enable_complex, 1, 4);

uint32_t ev;
while (appread(kpd, &ev, 0, 3) > 0) {   /* poll events */
    uint8_t key  = (ev >> 4) & 0xF;
    uint8_t type = ev & 0xF;
    ...
}
```

---

## super_spi

> 统一 SPI1+SPI2 主控，含 CS/DC/R1/R2 逻辑引脚 + DMA。**所有 mode 走 `SSPI_MODE(spi_inst, dev_id, op)` 宏**。

### Mode 编码

```c
#define SSPI_MODE(spi_inst, dev_id, op)  \
    (((((spi_inst)-1) & 1) << 6) | (((dev_id) & 3) << 4) | ((op) & 0x0F))
```

- bit 7-6: spi_inst 选择 (0=SPI1, 1=SPI2)
- bit 5-4: dev_id (0..SSPI_DEV_MAX-1, 每实例独立)
- bit 3-0: op (低 nibble)

### op 表 (低 nibble)

| op 宏 | 值 | 行为 |
|------|-----|------|
| `SSPI_CS_LOW` | 0x00 | dev CS↓ |
| `SSPI_CS_HIGH` | 0x01 | dev CS↑ |
| `SSPI_DC_LOW` | 0x02 | dev DC↓ |
| `SSPI_DC_HIGH` | 0x03 | dev DC↑ |
| `SSPI_R1_LOW` | 0x04 | R1↓ |
| `SSPI_R1_HIGH` | 0x05 | R1↑ |
| `SSPI_R2_LOW` | 0x06 | R2↓ |
| `SSPI_R2_HIGH` | 0x07 | R2↑ |
| `SSPI_SEND` | 0x08 | 裸 SPI 轮询发送 (不碰引脚) |
| `SSPI_SEND_CS` | 0x09 | CS↓ + 轮询发送 + CS↑ |
| `SSPI_SEND_CMD` | 0x0A | DC↓ + CS↓ + 发 1 字节 + CS↑ + DC↑ |
| `SSPI_SEND_DAT` | 0x0B | DC↑ + CS↓ + 发送 + CS↑ |
| `SSPI_SEND_DMA` | 0x0C | 裸 SPI DMA |
| `SSPI_SEND_CS_DMA` | 0x0D | CS↓ + DMA + CS↑ |
| `SSPI_SEND_DAT_DMA` | 0x0E | DC↑ + CS↓ + DMA + CS↑ |
| `SSPI_PULSE_R1` | 0x0F | R1↓ + delay 100us + R1↑ + delay 150us (硬件复位脉冲) |

### `SSPI_XFER` 系列 (全双工)

```c
#define SSPI_XFER        0x80
#define SSPI_XFER_INST(i) (0x80 | ((((i)-1) & 1) << 6))
```

`appwrite(spi, &xfer, sizeof(spi_xfer_t), SSPI_XFER_INST(1))`：

```c
typedef struct {
    void*    tx_buf;    uint16_t tx_len;
    void*    rx_buf;    uint16_t rx_len;
} spi_xfer_t;
```

走 SPI1/2 全双工轮询，**不碰 CS/DC**。

### `appcmd` 命令

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `reg` | `-i <inst>` | dev_id / -1 | 在 SPI1/2 注册一个设备，返回 0-based dev_id (每实例独立) |
| `setpin` | `-i <inst> -d <dev_id> -s <sel> -p <pin>` | 0/-1 | 设置设备逻辑引脚：sel ∈ {0=CS,1=DC,2=R1,3=R2}; pin = 全局引脚号 |
| `tx` | `-i <inst> -n <len>` | 字节数 / -1 | 裸 SPI 发送 `app->user_data` 中 len 字节 |
| 其它 | 见源码 `super_spi.c` | | |

`sspi_setpin(spi, inst, dev_id, sel, pin)` 是 C 助手函数（参见 `app_config.h`），等价于 `setpin` appcmd 但避免字符串解析。

### 引脚映射

`super_spi` 内部不硬编码引脚，所有 CS/DC/R1/R2 都通过 `setpin` 配置（KSCGUI / W25Q64 在 `appopen` 时主动调用）。常见工程映射：

| 实例 | CS | DC | RST |
|------|-----|-----|------|
| SPI2 (LCD) | PB12 (pin 28) | PB8 (pin 24) | PB9 (pin 25) |
| SPI1 (LCD 备用) | PA4 (pin 4) | PA2 (pin 2) | PA3 (pin 3) |
| SPI2 (W25Q64) | PB11 | — | — |

### 用法

```c
app_t* spi = appget("super_spi");
appopen(spi);

int tft1 = appcmd(spi, "reg -i 1");
appcmd(spi, "setpin -i 1 -d 0 -s 0 -p 4");  /* CS=PA4 */
appcmd(spi, "setpin -i 1 -d 0 -s 1 -p 2");  /* DC=PA2 */
appcmd(spi, "setpin -i 1 -d 0 -s 2 -p 3");  /* RST=PA3 */

appwrite(spi, NULL, 0, SSPI_MODE(1, tft1, SSPI_PULSE_R1));

uint8_t cmd = 0x11;
appwrite(spi, &cmd, 1, SSPI_MODE(1, tft1, SSPI_SEND_CMD));

uint8_t frame[1024] = {...};
appwrite(spi, frame, sizeof(frame), SSPI_MODE(1, tft1, SSPI_SEND_DAT_DMA));
```

---

## KSCGUI

> GUI Tile 合成器 + ST7789 驱动。**全部操作通过 `appcmd`**，没有 appwrite / appread 路径。

### 平台

- LCD: ST7789 240×320 竖屏
- SPI: 默认 SPI2（若 SPI2 不可用回退 SPI1）；运行时可用 `setspi` 切换
- 涵盖：硬件复位 (PULSE_R1) → 初始化序列 (SLPOUT/COLMOD/MADCTL/gamma) → SLPOUT

### 内存

KSCGUI 内部维护 16 个 `KSC_window` 槽 (capacity 16, 每槽含 `ksc_obj_t[]`) + 一个 active tile 指针 + `k_draw_device*`。

句柄 `tile_h_t (uint8_t)`：高 4 位 generation 防止 use-after-free，低 4 位 slot 索引。

### 命令总览

详细参考 `KSCOS/docs/KSCGUI_API.md`（命令以源码 `kscgui.c::gui_appcmds[]` 为准）。

#### 启动 / 切换

| 命令 | 参数 | 说明 |
|------|------|------|
| `init` | — | 初始化屏幕硬件 + 创建全屏 tile |
| `setspi` | `<inst>` | 切 SPI 实例 (1/2) |

> **跨平台契约（PC 与 STM32 一致）**：必须先 `init` 才能调用任何绘图命令
> （pixel/fill/rect/line/circle/arc/rrect/char/string/drawbmp/ibig/ibin/trender*）。
> 未 `init` 时绘图命令一律返回 `-1`（`gui_ctx.hw_inited` 拦截），tile 元数据操作
> （wcreate/wselect/wmove 等）不受限。PC 上 `init` 创建 easyx 窗口，未 init 时
> easyx 层还有 `GetHWnd()` 兜底防崩溃；STM32 上 `init` 跑 ST7789 初始化时序。

#### Tile 管理

| 命令 | 参数 | 说明 |
|------|------|------|
| `wcreate` | `-x <x> -y <y> -w <w> -h <h> -c <bk>` | 创建 tile，handle 存入 `app->callback_data` |
| `wdelete` | `-t <h>` | 删除 tile |
| `wselect` | `-t <h>` | 设为 active |
| `whide` / `wshew` / `wtoggle` | `-t <h>` | 隐藏 / 显示 / 切换可见 |
| `wmove` | `-t <h> -x <x> -y <y>` | 移动 tile |
| `wresize` | `-t <h> -w <w> -h <h>` | 重设尺寸 |
| `wbk` | `-t <h> -c <bk>` | 设背景 |
| `wzorder` | `-t <h> -z <z>` | 设 Z 序 (0=底) |
| `wactive` | — | 返回 active tile handle |
| `winfo` | `-t <h>` | 拿 `tile_info_t`（写 `app->callback_data`） |
| `wenum` | — | 枚举所有 tile（`app->user_data` + `callback_data`） |
| `wclear` | `-t <h>` 或省略=active | 清为 tile bk |

#### 渲染

| 命令 | 说明 |
|------|------|
| `tredraw -t <h>` | 重绘指定 tile |
| `trender -t <h>` | 渲染某 tile（差异细节见源码） |
| `trenderall` | 按 Z 序遍历所有 visible tile 重绘 |

#### 图元（坐标均为相对 active/screen tile）

| 命令 | 参数 | 说明 |
|------|------|------|
| `fill -x -y -w -h -c <color>` | 填充矩形 |
| `rect -x -y -w -h -c` | 矩形边框 |
| `line -x -y -w <x2> -z <y2> -c` | 直线 |
| `pixel -x -y -c` | 单像素 |
| `circle -x <cx> -y <cy> -r <r> -c` | 圆 (需 `__DRAW_CIRCLE__`) |
| `fcircle -x -y -r -c` | 填充圆 |
| `arc -x <cx> -y <cy> -r <r> -d <dir> -c` | 圆弧 (dir=0x01/0x02/0x04/0x08) |
| `rrect -x -y -w -h -r <radius> -c` | 圆角矩形 |
| `frrect -x -y -w -h -r -c` | 填充圆角矩形 |
| `char -x -y -v <ascii> -c <fg> -b <bg>` | 单字符 |
| `string -x -y -s <text> -c -b` | 字符串 |
| `image -x -y -w -h` | 从 `app->user_data` 绘 RGB565 图 |
| `ibin  -x -y -w -h -c -b` | 1-bit 二值图 |
| `ibig  -x -y -w -h -s <scale>` | 缩放图 |
| `drawbmp` | — | 从 open app 拉 BMP 数据流，解码 24-bit 并渲染到 active tile |


#### 对象

| 命令 | 说明 |
|------|------|
| `setobjs` | 设 KSC_object 数组到 active tile |
| `drawobjs` | 渲染对象数组 |
| `clear`/`-c` | 清屏为指定色 |

#### 颜色参数格式

`-c <color>` 为 16 进制 RGB565 (无 `0x` 前缀)；例如 `-c F800` 表示纯红。

### 用法

```c
app_t* gui = appget("KSCGUI");
appopen(gui);
appcmd(gui, "init");                                  /* 初始化 LCD */
appcmd(gui, "fill -x 0 -y 0 -w 240 -h 320 -c 0000");  /* 黑底 */
appcmd(gui, "rect -x 4 -y 4 -w 232 -h 312 -c FFFF");  /* 白边框 */
appcmd(gui, "circle -x 120 -y 100 -r 25 -c F800");    /* 红圆 */

int tile = appcmd(gui, "wcreate -x 0 -y 200 -w 240 -h 120 -c 001F");  /* 底部蓝条 */
/* tile handle 在 app->callback_data */
```

---

## list

> GUI 列表 widget，基于 KSCGUI 绘制；256B 字符串池 + 碎片管理 + 5 种选中样式。

### 容量

- 字符串池: 256B (碎片随移除增加；`compact` 可整理)
- 默认渲染: 当前项反白样式 (`LIST_STYLE_FILLBAR`)
- 列表项最多：受字符串池容量限制，索引 `uint8_t`

### `appcmd` 命令

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `init` | — | 0/-1 | 复位上下文（清空 + 默认） |
| `add` | `-d <label>` | 项数 / -1 | 追加项 label |
| `remove` | `-i <idx>` | 项数 / -1 | 删除项 idx |
| `clear` | — | 0/-1 | 清空所有项 |
| `compact` | — | 0/-1 | 手动碎片整理 |
| `select` | `-i <idx>` | 0/-1 | 选中项 idx |
| `move` | `-d <delta>` | new idx / -1 | 选中按 delta 移动 (1=下, -1=上) |
| `confirm` | — | 当前选中 idx | 触发 callback (提供 `app->callback`, `app->user_data`) |
| `getcount` | — | 项数 | 当前项数 |
| `getlabel` | `-i <idx>` | label 长度 | 通过 `app->user_data` 返回 label 字符串 |
| `setpos` | `-x <x> -y <y> -w <w> -h <h> -e <item_h>` | 0/-1 | 设绘制位置（`list_pos_t` 参数） |
| `setcolors` | `-p <sel_bg> -p?` 多色 | 0/-1 | 设 `list_colors_t` 含 sel_bg/bg/fg/sel_fg |
| `setstyle` | `-s <style>` | 0/-1 | 选中样式：0=NONE 1=FILLROW 2=FILLBAR 3=TEXTONLY 4=ARROW |
| `refresh` | — | 0/-1 | 强制重绘 |

### `appread` mode 表

| mode | data | 返回 | 说明 |
|------|------|------|------|
| 1 | `uint32_t*` | 4 | 当前 `selected` 索引 |
| 2 | `char* buf` | strlen+1（在 `count` 字段填 idx） | 取 idx 项 label（`count` 作为 idx 参数） |

### `appwrite` — no-op

list 无 `appwrite` 路径。

### 用法

```c
app_t* list = appget("list");
appopen(list);
appcmd(list, "init");
appcmd(list, "add -d \"Open\"");
appcmd(list, "add -d \"Save\"");
appcmd(list, "add -d \"Quit\"");
appcmd(list, "setstyle -s 2");       /* FILLBAR */
appcmd(list, "select -i 0");
appcmd(list, "refresh");              /* 重绘 */
```

---

### 默认按键 (计算器布局)

```
    [6]        →   上方向 
[9]    [11]        →   左 / 右
    [14]      →   下方向 
[10] 暂停/确认        [0] 退出
```

---

## snake

> Snake 游戏，全中断驱动 (TIM4 @ 250ms)，对象增量渲染。

### `appcmd` 命令

| 命令 | 参数 | 说明 |
|------|------|------|
| `init` | `-m <1/2>` | 1=阻塞模式（手动 step），2=中断模式（TIM4 驱动） |

### 中断模式

游戏循环由 TIM4 中断 (250ms) 调度：
1. 读 `button16` 事件队列 (mode=3, 仅 PRESS)
2. 处理方向 / 暂停 / 退出 / 重启
3. 更新游戏状态（碰撞 / 食物）
4. 渲染变化区段

退出 / 游戏结束后：任意键重新开始。

### 按键映射

```
    [6]        →   上方向 
[9]    [11]  ← / →
    [14]      →   下方向 
[10] 暂停     [0] 退出 → 回到 main loop
```

### 用法

```c
app_t* g = appget("snake");
appopen(g);
appcmd(g, "init -m 2");   /* 中断驱动模式 */
/* 主循环什么也不做；游戏在中断里运行 */
while (1) __WFI();
```

---

## w25qxx_base

> W25Q64 SPI NOR Flash 基础驱动，所有命令通过 `appcmd`，数据缓冲通过 `app->input_data`。
> CS 引脚 (默认 PB11) 由 `super_spi2` mode=5 运行时重映射。

> **⚠️ 破坏性命令警告**：`write` / `erase` / `ce` 会改写/擦除 W25Q64 原始数据，
> **直接摧毁 littlefs 文件系统**。littlefs 通过二进制接口（`appwrite` mode 1/3/5）使用 flash，
> 正常运行和测试**不需要**这些命令。仅开发者做底层调试时使用；执行 `ce`（全片擦除）=
> 删掉 `.data/flash.bin`。

### `appcmd` 命令

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `id` | — | JEDEC ID (`0xEF4017`) | 读 JEDEC ID |
| `sr` | — | 状态寄存器值 | 读 status register |
| `uid` | — | 8 (→ input_data) | 8B 唯一 ID 写入 `input_data` |
| `read` | `-a <addr> -n <len>` | 字节数 (→ input_data) | 标准读 (0x03) |
| `fast` | `-a <addr> -n <len>` | 字节数 (→ input_data) | 快速读 (0x0B + dummy) |
| ⚠️ `write` | `-a <addr> -n <len>` | 写入字节数 | **破坏性** 页写 0x02 (≤256B / page) |
| ⚠️ `erase` | `-a <addr> -s <size>` | 0/-1 | **破坏性** 擦除: size ∈ {4096, 32768, 65536} |
| ⚠️ `ce` | — | 0/-1 | **破坏性** Chip Erase (0xC7) = 清空整个 flash |

`-a / -n / -s` 接受 16 进制 (前缀 `0x`) 或 10 进制 (`strtoul(s, NULL, 0)`)。

### 用法

```c
uint8_t buf[256];
app->input_data = buf;
int n = appcmd(flash, "read -a 0 -n 256");

uint8_t page[256] = {...};
app->input_data = page;
appcmd(flash, "write -a 0 -n 256");   /* ⚠️ 破坏性 */

appcmd(flash, "erase -a 0 -s 4096");  /* ⚠️ 破坏性 */
```

> 只读命令（`id`/`sr`/`uid`/`read`/`fast`）供诊断使用；`write`/`erase`/`ce`
> 仅限开发者底层调试，会破坏 littlefs，切勿在正常流程/测试中调用。

---

## littlefs

> littlefs 文件系统，挂载在 W25Q64 上。`-p <path>` 必须绝对路径（`/` 开头）。

### 文件系统管理

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `format` | — | 0/-1 | 格式化 (须先 unmount) |
| `mount` | — | 0/-1 | 挂载 |
| `unmount` | — | 0/-1 | 卸载 |
| `info` | — | 0/-1 (→ user_data) | 输出 `blk_size=X blk_count=Y` |

### 一次性文件操作

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `writenew` | `-p <path> [-d <text>]` | 写入字节 / -1 | 覆写 (truncate then write) |
| `append` | `-p <path> [-d <text>]` | 写入字节 / -1 | 追加到末尾 |
| `cat` | `-p <path> [-n <max>]` | 字节 / -1 (→ user_data) | 读全文 |
| `ls` | `-p <path> [-n <max>]` | 字节 / -1 (→ user_data) | 每行: `D/F name size` |
| `rm` | `-p <path>` | 0/-1 | 删除 |
| `mkdir` | `-p <path>` | 0/-1 | 建目录 |
| `mv` | `-s <src> -d <dst>` | 0/-1 | 重命名 |
| `stat` | `-p <path>` | 0/-1 (→ user_data) | 输出 `type=F/D size=N name=...` |

### 持久文件 handle (fd 模式)

`open` 把 `lfs_file_t*` 存入 `app->callback_data`，后续命令从 `app->mode_data` 读 handle；`-h <ptr>` 可显式 override (ptr 为 16 进制指针)。

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `open` | `-p <path> [-f <flags>]` | 0/-1 (→ callback_data) | 默认 `LFS_O_RDONLY` (0x1)；`-f` 16 进制 flags |
| `close` | — | 0/-1 | close + free handle (取自 mode_data) |
| `fread` | `-n <max>` | 字节 / -1 (→ user_data / callback_data) | |
| `fwrite` | `[-d <text>]` | 写入字节 / -1 | `-d` 优先；否则用 user_data + `-n` |
| `fseek` | `-o <offset> [-w <whence>]` | 新位置 / -1 | whence: 0=SET, 1=CUR, 2=END |

flag 值 (`-f`)：

| 值 | 含义 |
|----|------|
| `0x1` | RDONLY |
| `0x2` | WRONLY |
| `0x3` | RDWR |
| `0x400` | CREAT |
| `0x200` | TRUNC |

### 用法

```c
/* 一次性写 */
appcmd(fs, "writenew -p /hello.txt -d \"Hello KSCOS\"");

/* 读 */
uint8_t buf[256];
app->user_data = buf;
int n = appcmd(fs, "cat -p /hello.txt");

/* 持久 fd */
appcmd(fs, "open -p /log.bin -f 0x403");   /* RDWR + CREAT */
/* handle 现在在 fs->callback_data，自动复制到 mode_data */
appcmd(fs, "fseek -o 0 -w 2");              /* SEEK_END */
uint8_t data[16] = {...};
app->user_data = data;
appcmd(fs, "fwrite -n 16");
appcmd(fs, "close");
```

---

## terminal

> 字符串路由分发器。接收 raw bytes (mode=0) 或完整命令行 (mode=1)，解析 `appname subcmd -x val` 并路由到目标 App 的 `appcmd`。

### `appwrite` mode 表

| mode | data | count | 行为 |
|------|------|-------|------|
| 0 | raw bytes | n | 攒行：缓冲到 80B `line_buf`，遇 `\r` 或 `\n` 触发整行路由；支持 `\b`/`\x7F` 退格 |
| 1 | 完整命令行 | n | 直接路由该行（跳过攒行）；自动去除尾部 `\r\n` |

### 内建命令

| 命令 | 说明 |
|------|------|
| `help` / `?` | 列出所有注册 App |
| `echo <text>` | 回显 text |

### 路由协议

输入 `appname subcmd -x val -y val`:

1. 第一个空格前 = `appname`
2. `appopen(target)` (幂等)
3. `term->user_data ≠ NULL` → target->user_data 复制 + target->output_fn = NULL（数据捕获模式）
4. `term->user_data == NULL` → target->user_data = NULL + target->output_fn = term_echo（控制台模式）
5. `appcmd(target, rest)` 调用目标，返回值原样返回

### 用法

#### 交互式 (idle loop 喂 UART byte)

```c
app_t* term = appget("terminal");
appopen(term);

while (1) {
    uint8_t c;
    if (appread(ksc_console, &c, 1, 1) > 0)
        appwrite(term, &c, 1, 0);     /* raw byte, 攒行 */
}
```

#### 程序化 (直接路由)

```c
appwrite(term, "help\r\n", 6, 1);              /* 列 app */
appwrite(term, "echo hello\r\n", 12, 1);       /* 内建 echo */
appwrite(term, "littlefs ls -p /\r\n", 17, 1); /* 路由到 littlefs */
```

---

## open

> 按扩展名路由文件打开。`file` 子命令用 pull 模型，GUI 通过 `appread` 拉数据流。

### 注册信息

`REGISTER_APP_EX("open", NULL, "2\0littlefs\0KSCGUI", &open_ops,
    "Route file open by extension to source app; 'file' subcmd for pull model")`

### 数据流

`open_read` 通过设置 littlefs 的 `callback_data` 调用 `fread` 实现数据拉取：

```
GUI (drawbmp) → appread(open, buf, n) → open_read
  → lfs->callback_data = data
  → appcmd_argv(lfs, "fread", ...)    ← 标准 littlefs 命令
  → lfs->callback_data = NULL
  → 返回读取字节
```

### `appcmd` 子命令: `file`

| 命令 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `file` | `-p <path>` | 0/-1 | 打开文件，按扩展名路由 |

路由行为：

| 扩展名 | 行为 |
|--------|------|
| `.bmp` | `littlefs open` → `appcmd(KSCGUI, "drawbmp")` → GUI 内部 `appread(open)` 拉数据 → 解码 → `kdrawimage` 渲染 |
| `.txt` / 其他 | `littlefs open` → 循环 `fread` → `output_fn` 推送 |

### 用例

```c
/* file 子命令 (推荐) */
appcmd(open_app, "file -p /notes.txt");            /* → output_fn 推送 */
appcmd(open_app, "file -p /photo.bmp");            /* → GUI drawbmp */

```

---

## 跨 App 共享类型 (`apps/app_config.h`)

| 类型 / 宏 | 用途 |
|----------|------|
| `sspi_mode_t { uint16_t len; }` | super_spi tx 动态参数 |
| `spi_xfer_t { void* tx_buf; uint16_t tx_len; void* rx_buf; uint16_t rx_len; }` | super_spi 全双工 |
| `SSPI_PIN_NONE` | 引脚未配置占位 |
| `SSPI_CS / SSPI_DC / SSPI_R1 / SSPI_R2` | 逻辑引脚选择 (0/1/2/3) |
| `SSPI_CS_LOW .. SSPI_PULSE_R1` | op 操作码宏 (见 super_spi 表) |
| `SSPI_MODE(i,d,op)` | mode 编码宏 |
| `SSPI_XFER / SSPI_XFER_INST(i)` | 全双工 mode 标记 |
| `tile_h_t` | KSCGUI tile 句柄 (uint8 4-bit gen + 4-bit slot) |
| `tile_info_t { handle,x,y,w,h,bk,visible,z,is_active,obj_count }` | tile 元信息 |
| `list_pos_t { x,y,w,h,item_h }` | list 位置 |
| `list_colors_t { sel_bg,bg,fg,sel_fg }` | list 颜色 |
| `LIST_STYLE_*` (NONE/FILLROW/FILLBAR/TEXTONLY/ARROW) | list 选中样式 |
| `CTRL_EVENT_CONFIRM / CTRL_EVENT_QUIT` | ctrl_list 事件 |
| `ctrl_keymap_t { up,down,ok,quit }` | ctrl_list 按键映射 |
| `ctrl_event_cb_t` | ctrl_list 回调类型 |
| `upload_ctx_t` | (upload 用，本文档省略) |

---

## 替换 / 增删 App 即完成程序构建

KSCOS 的设计准则是：**框架核心 (`inc/app.h` + `src/app.c` + `src/KSCOSsystem.c` + `src/KSCdraw.c`) 不绑定任何具体外设 / 库 / 平台**。本文件列出的 14 个 App（含内核 `system` app）都只是一份参考实现——上表所列 `app_dep` 仅表示"它们彼此之间的依赖关系"，运行时一切通过 `app_dep_str` 间接 `appget`。

### 替换某个内置 App

例：把 ST7789 屏驱动换成 ILI9341，不必改动 KSCGUI 本身——

1. 新建 `apps/ili9341_spi.c`，把 `super_spi` 当作依赖：
   ```c
   REGISTER_APP_EX("ili9341_spi", "0", "1\0super_spi", &ili9341_ops,
       "ILI9341 over super_spi");
   ```
2. 在目标工程的 `target_sources` 中**不加入** `kscgui.c` 或把它的内容并入你的新 GUI，让新 App 顶替原名 (`"KSCGUI"`) 即可。

框架仅以 `app_name` 索引 `app_table`，**同名后注册者覆盖前者**（注：实际由链接顺序决定，建议二选一时从 `target_sources` 列表去掉旧的）。`app_dep` 解析只认名字，不读其它字段——只要新 App 能满足同样 `appcmd` 协议（同名同命令），依赖它的其它 App（KSCGUI、list、ctrl_list 等）就无需重新编译。

### 删除某个 App

把对应 `.c` 文件从 `target_sources` 移除即可。如果有其它 App 的 `app_dep_str` 仍然写它为依赖，`appget` 会失败并返回 NULL——上游要么改 `app_dep_str` 减依赖，要么用同名空实现占位。

### 新增一个 App

参考模块根 `README.md` "扩展指南" 的最小模板。要点：
- 一个 `.c` 一个 `REGISTER_APP_EX`（不要在一个文件写多个）
- `app_dep_str` 用 `"N\0name1\0..."` 串接依赖，最多 4 个
- App 在 `appopen` 中 `osmalloc` 出 `app_data`，`appclose` 中 `osfree`，绝不使用 static 全局承载 per-instance 状态
- `appcmd` 派发用 `const {name, handler}[]` 静态表
- 把新 `.c` 加入到目标可执行文件的 `target_sources` 即可（不能进 `add_library`，否则 LTO 会把 `app_table` 段当作 orphan 丢弃）

### 第三方库 = 可替换实现

| 第三方 | 仅为哪个 App 提供 | 替代候选 |
|--------|------------------|---------|
| easyx | PC 平台 KSCdraw 屏呈现 | SDL2 / raylib / 自绘后端 |
| littlefs (`third_party/littlefs/`) | `littlefs` App 的 FS 实现 | FatFS / 自实现 FS |
| tjpgd3 | JPEG 解码 | stb_image / libjpeg-turbo |
| STM32 HAL 桩 (`third_party/stm32/`) | `uart_serial` / `super_spi` / `tim_clock` 所需寄存器宏 | CubeMX HAL / LL 库 / 裸 CMSIS |

