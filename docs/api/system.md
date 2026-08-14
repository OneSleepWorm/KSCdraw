# System API

> 声明: `inc/KSCOSsystem.h` · 内核服务: `inc/kscsystem.h` + `bsp/<平台>/system.c` · 分发实现: `src/KSCOSsystem.c`

系统层提供三件事：**固定地址的 system app**（时间/内存/芯片初始化）、**固定地址的 console app**（printf/终端路由）、**错误处理**。核心架构原则是「内核服务即 app」：`system` 和 `console` 是注册在 app_table 里的**固定地址特殊 app**，普通 app 经 `appget("system")` / `appget("console")` 访问。

## 架构总览

```
main
  ├─ appget("system")      → SYSTEMAPP 固定地址, 自动 open (A4 补丁)
  │    └─ system.open      → mempool_init + 芯片初始化 (STM32: PLL/SysTick)
  ├─ appget("console")     → CONSOLEAPP 固定地址, 自动 open (A4 补丁)
  │    └─ console.open     → 经 app_dep 依赖 uart_serial (app0)
  └─ sys_init()
       ├─ appget("uart_serial") → appopen → appcmd(uart,"open")   (引导层)
       ├─ appget("console")     → appopen
       └─ appget("terminal")    → appopen

osdelay / os_gettime / os_malloc / os_free / os_calloc   (fastsystem.h 内联宏)
  └─ appwrite(SYSTEMAPP, ..., mode) → system_app_write 分发 (mempool / 时间)

kscprintf / kscterminal
  └─ appwrite(CONSOLEAPP, ...) / appread(CONSOLEAPP, ...) → 路由到 uart
```

| 组件 | 位置 | 职责 |
|------|------|------|
| `inc/kscsystem.h` | 内核接口 | `SYSTEMAPP`/`CONSOLEAPP` 宏、固定 app 类型、命令常量 |
| `inc/mempool.h` | 内存池接口 | 多档块池 + 统计结构 |
| `bsp/share/include/fastsystem.h` | 便捷封装宏 | os*/sys* 内联封装（非 API，仅开发者便利）|
| `bsp/{stm32,pc}/system.c` | system app | open/read/write/cmd，时间/内存/芯片初始化 |
| `bsp/{stm32,pc}/system_zone.c` | 固定区 | SYSTEMAPP 固定地址 |
| `bsp/share/src/console.c` | console app | 全局加载路由，依赖 uart_serial |
| `bsp/{stm32,pc}/console_zone.c` | 固定区 | CONSOLEAPP 固定地址 |
| `bsp/share/src/mempool.c` | 内存池 | 6-7 档块池，零碎片 |
| `src/KSCOSsystem.c` | 共享层 | kscprintf/kscterminal/sys_init + 全局定义 |

## SYSTEMAPP / CONSOLEAPP 固定地址

`SYSTEMAPP`（`ksc_system_app`）与 `CONSOLEAPP`（`ksc_console_app`）指向**固定地址**的 app_t，不走 osmalloc：

- **STM32**: 链接脚本 `.system_zone` / `.console_zone` 段（`.bss` 之后、heap 之前，独占 RAM）。见 `third_party/stm32/STM32F103XX_FLASH.ld`。
- **PC**: 静态全局数组（逻辑固定，运行时地址不变）。

两个固定 app 在 `appget` 时被识别为特殊分支（`is_fixed`）：直接使用固定区、入缓存前立即 open（A4 补丁）。`appopen` 幂等返回 0，`appclose` 拒绝（返回 -1）。

**console app** 不是 uart——它是**全局加载路由**，通过标准 `app_dep` 依赖 `uart_serial`（app0），`write`/`read` 直接路由到 uart，与普通 app 用法一致。

## 内存池 (mempool)

所有内存分配（经 `fastsystem.h` 宏 → SYSTEMAPP）最终落到 mempool——**多档块池**，固定大小分档、零碎片、地址运行时固定：

| 档 | 块大小 | 块数 | 用途示例 |
|----|--------|------|---------|
| 0 | 32B | 16 | 小对象、app_cache_node |
| 1 | 64B | 16 | 小 ctx |
| 2 | 128B | 16 | 中等对象 |
| 3 | 256B | 12 | littlefs lookahead 缓冲 |
| 4 | 512B | 8 | littlefs read/prog 缓冲 |
| 5 | 1KB | 4 | KSCGUI ctx |
| 6 (PC only) | 2KB | 2 | `pc_uart_ctx_t` (1256B) |

> PC 额外有 2KB 档（`MEMPOOL_CLASS_MAX` 按平台区分：PC=7，STM32=6）。STM32 RAM 仅 20KB，池 14848B 占用 87.5%，须严格控制各档块数。

### 运行时统计

`system mem` 命令输出各档占用、历史峰值、累计分配/释放次数、分配失败计数：

```
pool fail=0 used=5152B/18944B peak=5728B
    32B  5/16 peak= 5 a=5 f=0
   64B  0/16 peak= 1 a=1 f=1
  128B  5/16 peak= 6 a=7 f=2
  256B  1/12 peak= 1 a=1 f=0
  512B  4/ 8 peak= 5 a=6 f=2
 1024B  0/ 4 peak= 0 a=0 f=0
 2048B  1/ 2 peak= 1 a=1 f=0
```

字段含义：`a` = 累计分配次数，`f` = 累计释放次数，`peak` = 该档历史最大同时占用，`fail` = 分配失败次数（池满/超档）。`a-f` 的差值应等于当前 `used`，可据此检测泄漏。

## 全局符号

| 符号 | 类型 | STM32 | PC |
|------|------|-------|----|
| `KSCOSsystem_Clock` | `volatile uint32_t` | 系统时钟频率 (Hz)，由 system open 时写入 (默认 72000000) | 占位 |
| `sys_tick_ms` | `volatile uint32_t` | SysTick 中断自增的 ms tick (1 ms 周期) | 占位 (PC 走 Windows 计时) |

> `ksc_console` / `ksc_term` 全局变量已**废除**——由固定地址 `CONSOLEAPP` 取代。os*/sys* 系列已收敛为 `fastsystem.h` 内联宏，无全局符号。正式接口只有：app 系列函数 + SYSTEMAPP + CONSOLEAPP。

## 时间服务

### `sysdelay`

```c
void sysdelay(uint32_t ms);
```

阻塞延时 `ms` 毫秒。经 `appwrite(SYSTEMAPP, &ms, 4, 2)` 分发。
- STM32: 基于 `sys_tick_ms` 差值轮询
- PC: `Sleep(ms)`

### `sysgettime`

```c
uint32_t sysgettime(void);
```

经 `appread(SYSTEMAPP, &t, 4, 0)` 分发，返回 ms 计数。约 49 天后回绕（正常边界）。

### system app 命令

| 命令 | 说明 |
|------|------|
| `system time` | 输出当前 tick 到 `output_data` |
| `system delay -t <ms>` | 阻塞延时 |
| `system idle` | 空闲等待 (STM32: WFI; PC: Sleep(1)) |
| `system mem` | 输出内存池统计 (见上文) |

## 堆分配 (fastsystem.h 内联宏)

```c
void* os_malloc(size_t size);
void  os_free(void* ptr);
void* os_calloc(size_t num, size_t size);
```

定义于 `bsp/share/include/fastsystem.h`，是 **static inline 宏封装**（非全局函数，不产生符号），经 system app 二进制接口分发：
- `appwrite(SYSTEMAPP, &ptr, size, 0)` = malloc
- `appwrite(SYSTEMAPP, &ptr, sizeof(ptr), 1)` = free

> 旧名 `osmalloc/osfree/oscalloc/sysdelay/sysgettime/osdelay/oswait_idle` 通过兼容宏映射到新内联封装。它们**不是正式 API**，只是开发者便捷封装——正式接口只有 app 系列函数 + SYSTEMAPP + CONSOLEAPP。

**约束**：池有固定容量。分配超过最大档（PC 2KB / STM32 1KB）或某档耗尽会返回 NULL，`alloc_fail` 计数递增。用户代码应检查返回值。

> **1 KB 栈约束**: STM32F103C8 的栈仅 0x400 = 1024 字节。稍大的本地数组（如 `uint8_t buf[256]` 配合其它调用栈）极可能无声溢栈。**所有 per-instance 数据必须 `osmalloc`**，不可用 static 全局数组承载运行期状态。

## 控制台与打印

### `kscprintf`

```c
void kscprintf(const char* fmt, ...);
```

内部 `vsnprintf` 到 `char buf[128]`，经 `appwrite(CONSOLEAPP, buf, len, 0)` 输出——console app 路由到 uart。
- `CONSOLEAPP == NULL` 直接返回；上电后第一次使用前必须先调过 `appget("console")` / `sys_init()`

### `__io_putchar`

```c
int __io_putchar(int ch);
```

newlib / printf 重定向钩子。经 `appwrite(CONSOLEAPP, &c, 1, 0)` 输出单字节。PC 上不提供。

### `kscterminal`

```c
int kscterminal(void);
```

终端轮询：`appread(CONSOLEAPP, ...)` 读输入 → `appwrite(terminal, ...)` 路由到终端 app。

## 系统初始化

### `sys_init`

```c
void sys_init(void);
```

序列：
1. `appget("uart_serial")` → `appopen` → `appcmd(uart, "open")`（引导层打开 uart 通道）
2. `appget("console")` → `appopen`（固定 CONSOLEAPP，内部依赖 uart）
3. `appget("terminal")` → `appopen`

> 注意：上电后 `main` 应先 `appget("system")` + `appget("console")`（固定起手招），再 `sys_init()`，之后才能执行任何 `appwrite` / `kscprintf`。

### 芯片初始化 (STM32, system.open 内部)

`system_platform_init` 序列：
1. `RCC->APB2ENR |= AFIOEN`，`AFIO->MAPR |= 1`（关闭 JTAG，释放 PB3/PB4 作普通 IO）
2. PLL 配置: HSE 8MHz × 9 = 72MHz, FLASH latency=2, AHB=DIV1/APB1=DIV2/APB2=DIV1
3. `SysTick_Config(CLOCK/1000)` → 1ms tick → `sys_tick_ms` 在 `SysTick_Handler` 自增
4. 写 `KSCOSsystem_Clock` / `data->clock`

## 错误处理

### `KSCOS_Error_Handler`

```c
void KSCOS_Error_Handler(void);
```

STM32：`__disable_irq()` + `while(1)` 死循环。生产代码中应替换为日志 + 复位（当前为占位）。

### `KSCOS_default_Error_Handler`

```c
ki8 KSCOS_default_Error_Handler(void* data);
```

接受 `void*` 的版本，便于塞进回调槽。返回 `-1` 后调用 `KSCOS_Error_Handler`，不再返回。

## 平台区别速查

| 项 | STM32 | PC |
|----|-------|----|
| system app 位置 | `.system_zone` 段 (固定地址) | 静态全局数组 |
| console app 位置 | `.console_zone` 段 (固定地址) | 静态全局数组 |
| 池档数 | 6 (32B~1KB) | 7 (32B~2KB) |
| 芯片初始化 | `system_platform_init` 实操 PLL/SysTick | 无 |
| `sysdelay` | 基于 `sys_tick_ms` 阻塞 | `Sleep(ms)` |
| `sysgettime` | `sys_tick_ms` | `GetTickCount()` |
| `kscprintf` | `vsnprintf` → CONSOLEAPP → uart | 同 (file transport) |
| 内存分配 | fastsystem.h 宏 → mempool | 同 |
| `sys_init` | uart + console + terminal | 同 |

## 用法范式

```c
int main(void) {
    appget("system");                /* 固定起手招: 内核服务 */
    appget("console");               /* 固定起手招: printf/终端路由 */
    sys_init();                      /* 装配 uart/console/terminal */

    kscprintf("boot\r\n");           /* 经 CONSOLEAPP 输出 */

    while (1) {
        kscterminal();               /* 终端轮询 */
    }
}
```

实际工程的 `main` 函数除 `sys_init()` + idle loop 之外，可能还要挂其它中断（WatchDog、GCB tick 等）——它们与本框架无关：KSCOS 只占用 SysTick + 用到的外设 NVIC IRQn（由各 App 自行启用），不冻结宿主代码。
