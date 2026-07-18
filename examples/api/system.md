# System API

> 声明: `inc/KSCOSsystem.h` · 实现: `src/KSCOSsystem.c`

系统层提供三件事：时钟与时间、堆分配、控制台输出。所有接口在 STM32 与 PC 上都有同名实现，但语义可能略不同（PC 为便于调试通常为空实现或遗留 bus 调用）。

## 全局符号

| 符号 | 类型 | STM32 | PC |
|------|------|-------|----|
| `KSCOSsystem_Clock` | `volatile uint32_t` | 系统时钟频率 (Hz), 由 `pll_init` 写入 (默认 72000000) | 由 `KSCOSSystemClock_Init` 写 8000000 |
| `sys_tick_ms` | `volatile uint32_t` | SysTick 中断自增的 ms tick (1 ms 周期) | 不存在 (PC 走其它计时) |
| `ksc_console` | `app_t*` / `dd_t*` | `appget("uart_serial")` 实例 1, `sys_init` 设置 | 遗留 `dd_t*`, 由 bus 查找 |

`sys_tick_ms` 与 `KSCOSsystem_Clock` 的初值在 `pll_init` 完成后才稳定；用户代码通常在 `sys_init()` 之后使用。

## 时钟与时间

### `KSCOSSystemClock_Init`

```c
void KSCOSSystemClock_Init(uint8_t clock_type);
```

| 常量 | 值 | STM32 行为 |
|------|-----|----------|
| `KSCOS_LOW_CLOCK` | 0 | 当前实现：仅设置 `KSCOSsystem_Clock = 8000000`，不切 PLL |
| `KSCOS_NORMAL_CLOCK` | 1 | 同上 |
| `KSCOS_HIGH_CLOCK` | 2 | 同上 |

> **STM32 真正的 PLL 配置在 `sys_init()` 内部 `pll_init()` 中完成**，不在本函数。`clock_type` 当前实现忽略。若用户需要自定义时钟频率，应改写 `pll_init()` 而不是调本函数。

### `pll_init` (STM32 内部)

实际配置序列（`src/KSCOSsystem.c:25-57`）：
1. 计算 sysclk 与对应 FLASH latency (sysclk≤24M=0, ≤48M=1, 否则 2)
2. 使能 HSE → 选 PLLSRC + PLLMUL
3. 使能 PLL → 切换 SW=PLL
4. 设 AHB=DIV1, APB1=DIV2, APB2=DIV1
5. `KSCOSsystem_Clock = SystemCoreClock = 72000000`
6. `SysTick_Config(CLOCK/1000)` → 1ms tick → `sys_tick_ms` 在 `SysTick_Handler` 自增

固定参数 `mul = 9`（即 HSE 8MHz × 9 = 72MHz）。

### `sysdelay`

```c
void sysdelay(uint32_t ms);
```

阻塞延时 `ms` 毫秒。基于 `sys_tick_ms` 差值轮询，不进入 WFI。可被中断嵌套打断后回到本函数继续等待。

### `sysgettime`

```c
uint32_t sysgettime(void);
```

返回 `sys_tick_ms` 当前值。约 49 天后回绕（uint32_t ms ≈ 4.29×10⁹，U2 的 wrap 是正常边界）。

## 堆

### `osmalloc / osfree / oscalloc`

```c
void* osmalloc(size_t size);
void  osfree(void* ptr);
void* oscalloc(size_t num, size_t size);
```

STM32 上为 libc `malloc/free/calloc` 的简单包装；链接脚本预留了 `_end` 之后的内存为堆。KSCOS 内部所有 `app_data` 上下文、littlefs 缓冲等都走这套接口。

PC 上同样为 libc 包装。

> **1 KB 栈约束**: STM32F103C8 的栈仅 0x400 = 1024 字节。稍大的本地数组（如 `uint8_t buf[256]` 配合其它调用栈）极可能无声溢栈。**所有 per-instance 数据必须 `osmalloc`**，不可用 static 全局数组承载运行期状态——多个同 name App 实例（理论上单例，但单例靠运行期缓存隔离）也要求堆分配以隔离上下文。

## 控制台与打印

### `kscprintf`

```c
void kscprintf(const char* fmt, ...);
```

内部 `vsnprintf` 到 `char buf[128]`，调用 `appwrite(ksc_console, buf, len, 0x11)` 输出。
- `mode 0x11 = (inst=1)<<4 | op=1` (UART 轮询发送)
- `ksc_console == NULL` 直接返回；上电后第一次使用前必须先调过 `sys_init()`

### `__io_putchar`

```c
int __io_putchar(int ch);
```

newlib / printf 重定向的钩子。通过 `appwrite(ksc_console, &c, 1, 0x01)` 输出单字节。
- 检查 `ksc_console && ksc_console->app_data`，未初始化时丢弃字符。

PC 上不提供此函数。

### `ksc_console`

```c
extern app_t* ksc_console;       /* STM32 */
extern dd_t*  ksc_console;       /* PC (遗留 bus 路径) */
```

- STM32: `sys_init()` 末尾 `ksc_console = uart_serial` 实例。
- PC: `sys_init()` 调 `bus_getdriver(KSC_CONSOLE_DRIVER)`（"uart_printf_1" 或 "sys_console"），通过 `ddioctl` 输出。

`KSC_CONSOLE_DRIVER` 在 `KSCconfig.h` 中按平台定义。

## 系统初始化

### `sys_init`

```c
void sys_init(void);
```

STM32 序列：
1. `RCC->APB2ENR |= AFIOEN`，`AFIO->MAPR |= 1`（关闭 JTAG，释放 PB3/PB4 作普通 IO）
2. `pll_init()` 切 72MHz + SysTick
3. `app_t* uart = appget("uart_serial")` → `appopen(uart)` → `appcmd(uart, "open -i 1")` 使能 USART1
4. `ksc_console = uart`

> 注意：上电后系统应先调 `sys_init`，再执行任何 `appwrite` / `kscprintf`。否则 `ksc_console` 为 NULL，输出会被静默丢弃。

PC：调 `bus_init` / `bus_getdriver`，遗留架构。

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
| 时钟配置 | `pll_init` 实操寄存器 | 不设 |
| `sysdelay` | 基于 `sys_tick_ms` 阻塞 | `(void)ms` 空操作 |
| `sysgettime` | `sys_tick_ms` | 返回 0 |
| `kscprintf` | `vsnprintf` → UART1 appwrite | 无 |
| `osmalloc` 系列 | libc `malloc`/`calloc`/`free` | 同 |
| `ksc_console` | `app_t*` | `dd_t*` |
| `sys_init` | PLL+UART app | bus 框架 |
| `KSCOS_Error_Handler` | `__disable_irq` 死循环 | 仅 `while(1)` |

## 用法范式

```c
int main(void) {
    sys_init();                              /* 必须最先调: 配 PLL+UART console */
    kscprintf("boot\r\n");

    app_t* term = appget("terminal");
    if (term) appopen(term);

    while (1) {
        uint8_t c;
        if (appread(ksc_console, &c, 1, 1) > 0)
            appwrite(term, &c, 1, 0);
    }
}
```

实际工程的 `main` 函数除 `sys_init()` + idle loop 之外，可能还要挂其它中断（WatchDog、GCB tick 等）——它们与本框架无关：KSCOS 只占用 SysTick + 用到的外设 NVIC IRQn（由各 App 自行启用），不冻结宿主代码。