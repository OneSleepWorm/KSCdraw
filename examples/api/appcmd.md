# `appcmd` 字符串接口规范

> 声明: `inc/app.h` · 实现: `src/app.c::appcmd()`

`appcmd` 是 KSCOS 的统一入口：所有 App 都以 `appcmd(app, "cmd -x val -y val")`方式接收命令，由框架统一解析出命令名与 26 个 flag 槽位再派发给 App 的 `ops->cmd`。

## 签名

```c
int appcmd(app_t* app, const char* cmdline);
int appcmd_argv(app_t* app, const char* cmdname, const char** argv);
```

- `appcmd`: 自带解析（栈静态 buffer 128B，**非可重入**）
- `appcmd_argv`: 跳过解析，直接接收已构造好的 `cmdname` 与 `argv[26]`，可在中断上下文使用，或避免反向字符串化

## 语法

```
cmd -x val1 -y val2          # flag 顺序任意
cmd                          # 无参数命令
```

- 第一个 token 为命令名 `cmdname`（必须非 flag 形式）
- 后续以 `-<a-z>` 形式记 flag，共 26 个槽（`a..z`），每个 flag 可带一个字符串值
- 框架不区分 flag 出现顺序；命令实现侧顺序无关检查
- App 自己的命令名约定见 [`apps.md`](apps.md)

## 宏

```c
#define APPCMD_ARG(c)   ((c) - 'a')        /* 槽索引: argv['x'-'a'] */
#define APPCMD_HAS(argv, c)  ((argv)[APPCMD_ARG(c)] != NULL)   /* flag 是否出现 */
```

App 命令处理内部使用模式：

```c
static int cmd_foo(app_t* app, const char** argv) {
    if (!APPCMD_HAS(argv, 'p')) return -1;
    const char* path = argv[APPCMD_ARG('p')];
    int n = APPCMD_HAS(argv, 'n') ? atoi(argv[APPCMD_ARG('n')]) : 0;
    ...
}
```

> `APPCMD_HAS` 中的 `c` 必须是 `'a'`..`'z'` 的整型常量；若使用变量表达式需先升 `int`，避免编译警告。`APPCMD_ARG('x')` 展开为常量 `23`，可直接做数组下标。

## 引号与转义

由 `appcmd()` 在内部 `static char buf[128]` 中就地解码，不会溢出。

| 值形式 | 示例 | 规则 |
|--------|------|------|
| 无引号 | `-d hello` | 遇空格或行尾结束；不支持值内空格 |
| 双引号 | `-d "hello world"` | 支持转义序列（见下）；遇未转义 `"` 结束 |
| 单引号 | `-d 'hello world'` | 纯字面值；**不支持转义**；遇 `'` 结束 |

### 双引号内支持的转义

| 序列 | 解码 |
|------|------|
| `\"` | `"` (0x22) |
| `\\` | `\` (0x5C) |
| `\n` | LF (0x0A) |
| `\r` | CR (0x0D) |
| `\t` | TAB (0x09) |
| `\0` | NUL (0x00) |
| `\x` (其它) | 保留字面 `\x`（反斜杠 + 字符） |

> `appcmd` **永远以 `\0` 终止解码结果**并存入 argv 槽位，所以传入数据可含内嵌 `"\0"`；但若 App 实现侧用 `strlen(argv[i])` 处理就会截断。littlefs `fwrite` 等需要二进制长度的命令通过 `-n <len>` 显式给出。

## 参数解析细节

`appcmd()` 的实际行为（见 `src/app.c:166-237`）：

1. 全部 argv 槽预置为 `NULL`。
2. 命令名取第一空格前 token，遇到第二个字符为空格则替换为 `\0` 切分。
3. 进入 flag 循环:
   - `*p == '-'` 且 `p[1] in ['a','z']` → 识别为 flag
   - 紧随其后若无 `-x` 形式 token 则作为该 flag 的值；否则记为空串 `""`
   - `argv[idx]` 指向 buf 中解码后的子串
4. 其它格式（如 `--foo` 或数字裸值）会 break 循环，剩余字符串被丢弃
5. 调 `ops->cmd(app, cmdname, argv)`，将 `argv` 与原始 `cmdline` 无关地传入

### 值为空的情况

| 写法 | `APPCMD_HAS(argv, 'x')` | `argv[APPCMD_ARG('x')]` |
|------|-----------|------|
| `cmd -x` (行尾) | true | `""` |
| `cmd -x -y v` | true | `""` |
| `cmd -x ""` | true | `""` |
| `cmd -x "hello"` | true | `"hello"` |
| 未出现 | false | `NULL` |

注意 `""` 与 `NULL` 的区别：前者表示 flag 出现但值为空，后者表示 flag 未出现。检测时统一用 `APPCMD_HAS`，不要直接 `argv[idx]` 判 NULL 是否 flag 出现。

### 长度限制

- `cmdline` 总长 ≤ `APPCMD_LINE_MAX` (128) 字节，超出被静默截断
- `appcmd` 内部静态 `static char buf[128]` 编解码，缓冲只存活于调用过程
- `static const char* argv[26]` 同样是 `appcmd` 内静态，回调期间有效

## 数据通道

`appcmd` 自身只传字符串 argv；二进制 / 大块数据通过 `app_t` 的字段传递：

| 字段 | 用途 | 桁架 |
|------|------|------|
| `app->user_data` | 调用方提供的数据缓冲 (tx 或 rx) | 调用方负责生命周期 |
| `app->output_fn/ctx` | 流式输出回调 (分块回调) | 调用方注册 |
| `app->callback_data` | 持久句柄 (文件 fd / tile handle / 注册的 dev_id) | App 自身维护 |
| `app->callback` | 事件通知 (RX 到达 / 列表确认) | 注册方 |

### 输出类的三态优先级

输出命令（`cat`/`ls`/`read`/`info`/`stat`/`uid`/`ls`/`ls_home`等）内部按以下优先级：

1. `app->user_data ≠ NULL` → 写入 `user_data`，返回写入字节数
2. `app->output_fn ≠ NULL` → 分块调用 `output_fn(data, len, output_ctx)`，返回累计字节数
3. 两者皆 NULL → 返回 `-1`

terminal 自动管理这两通道：当 `term->user_data` 非 NULL 时把 target 的 `output_fn` 置 NULL；当为 NULL 时把 target 的 `output_fn` 设为 `term_echo`，自动打印到控制台。

### 数据传递范式

#### 短字符串

```c
appcmd(fs, "writenew -p /f -d hello");
```

#### 二进制块 (用 user_data + `-n`)

```c
uint8_t buf[256];
fill(buf);
app->user_data = buf;
appcmd(spi, "tx -i 2 -n 256");
/* 返回值 = 实际发送字节数 */
```

#### 输出捕获 (用 user_data)

```c
uint8_t buf[256];
app->user_data = buf;
int n = appcmd(fs, "cat -p /f");
/* app->user_data 写入 n 字节 */
```

#### 流式输出

```c
app->output_fn = my_sink;
app->output_ctx = my_ctx;
appcmd(fs, "cat -p /bigfile");
/* my_sink 被分块调用直至 EOF */
```

## 返回值约定

| 范围 | 语义 |
|------|------|
| `≥ 0` | 成功。具体语义随命令（字节数 / handle / 状态值 / 索引） |
| `< 0` | 错误 |

- 几乎所有 App 的"读字节数"语义都对齐 POSIX 风格：正常返回 `0` 表示 EOF / 无数据；返回值为字节数；`-1` 错误。
- 创建类命令（`wcreate`/`reg`/`open`）返回值为生成的句柄 ID 或 0；具体见 [`apps.md`](apps.md) 各表。

## mode 字节编码

许多 `appread` / `appwrite` / `appcmd` 调用要求 `mode` 字节携带硬件实例号 + 操作码：

```
mode = (inst << 4) | op
        ^^^^         1-indexed 硬件实例 (USART1/2/3, TIM1..4, SPI1/2)
             ^^^^^^ 操作码 (低 nibble)
```

| op 范围 | 通用约定 |
|---------|---------|
| `0` | noop |
| `1` | set / period / 设参数 |
| `2` | start / stop |
| `4` | init |
| `5` | register (注册返回 ID) |
| `6+` | 各 App 自定义 |

`super_spi` 的 mode 编码见 `apps/app_config.h` 的 `SSPI_MODE(spi_inst, dev_id, op)` 宏（编码多出 dev_id 2 位，详见 [`apps.md`](apps.md#super_spi)）。

## 非 appcmd 路径

下列情况绕过 `appcmd` 字符串路径，直接走 `appread/appwrite` 或 `appcmd_argv`:

- 二进制/块数据 (UART 发送、SPI DMA、文件读写)
- 中断上下文调用（`appcmd` 用静态 buffer）
- 性能热点（避免字符串解析开销）

```c
/* 例子: 中断里直接调 uart appwrite 发送 */
void USART1_IRQHandler(void) {
    ...
    appwrite(uart, buf, n, 0x11);   /* mode 0x11 = (inst=1)<<4 | op=1 轮询发送 */
}
```

## appcmd 的派发栈

```
caller
  │
  │  appcmd(app, "writenew -p /f -d hi")
  ▼
app.c::appcmd
  │  parse -> cmdname="writenew", argv['p']="/f", argv['d']="hi"
  ▼
app->ops->cmd(app, "writenew", argv)
  │
  ▼
lfs_cmd() → look up `lfs_cmd_entry_t` table
  │
  ▼
cmd_writenew(app, argv)
  │
  │  ... 写文件 ...
  ▼
return 3   → appcmd 原 return → 调用方
```

## 实现注意

- **静态 buffer**: `appcmd` 不可重入。所有中断上下文请改用 `appcmd_argv`。
- **`-n` 一般十进制**: `appread`/`appwrite` 的 mode 不参与 appcmd；某些命令的 `-n`/`-a` 允许 16/10 进制（`strtoul(s, NULL, 0)` 解析），见 App 自身的实现。
- **长命令拆分**: 128B 上限。写大文件用 `writenew`(truncate) + `append` 接力，或用 `fopen` + `fwrite` 持久句柄模式，而不是把整段内容塞 appcmd。
- **flag 字母大小写**: 仅支持小写 `-a`..`-z`；`-A`/`--help` 之类不会识别。