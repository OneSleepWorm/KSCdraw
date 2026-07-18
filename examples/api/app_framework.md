# App Framework API

> 对应头文件: `inc/app.h` · 实现: `src/app.c`

KSCOS 的所有外设 / GUI / 文件系统 / 工具都以 **App** 为单位组织。App 在编译期通过 `REGISTER_APP` 宏登记到 `app_table` ELF 段，在运行期通过统一的六个 API 调用。

## 核心数据结构

### `papp_t` — 编译期常量

```c
typedef struct __attribute__((aligned(16))) papp_t {
    const papp_base_t* base;        /* {app_name} */
    const char*        dep_str;     /* pdrv 依赖, 已废除, 一律填 "0" */
    const char*        app_dep_str; /* App 间依赖 "N\0name1\0name2\0..." */
    const papp_ops_t*  ops;         /* 5 回调 */
} papp_t;
```

由 `REGISTER_APP` 宏以 `__attribute__((section("app_table"), used))` 放入 ELF 段；链接脚本定义 `__start_papp_table` / `__stop_papp_table`。

### `papp_ops_t` — 回调表

```c
typedef struct papp_ops_t {
    papp_open_func   open;    /* int    (app_t* app)                        */
    papp_close_func  close;   /* int    (app_t* app)                        */
    papp_read_func   read;    /* int    (app_t* app, void* data,            */
                              /*         uint32_t count, uint32_t mode)     */
    papp_write_func  write;   /* int    (app_t* app, void* data,            */
                              /*         uint32_t count, uint32_t mode)     */
    papp_cmd_func    cmd;     /* int    (app_t* app, const char* cmdname,    */
                              /*         const char** argv)                 */
} papp_ops_t;
```

任一回调可为 NULL（不需要某功能时，但 `open` 几乎所有 App 都有）。

### `app_t` — 运行期实例

```c
typedef struct app_t {
    const papp_t*       papp;
    struct app_t*       app0;             /* 依赖 0 */
    struct app_t*       app1;             /* 依赖 1 */
    struct app_t*       app2;             /* 依赖 2 */
    struct app_t*       app3;             /* 依赖 3 */
    const papp_ops_t*   app_ops;
    void_func_t         callback;         /* 事件回调 (RX/选中) */
    app_output_fn       output_fn;        /* 流式输出重定向 */
    void*               output_ctx;
    void*               app_data;         /* App 内部上下文 (open 时 osmalloc) */
    void*               user_data;        /* 调用方数据缓冲 (appcmd 输入/输出) */
    void*               callback_data;    /* 持久句柄 (fd/tile handle) */
    void*               mode_data;        /* 当前模式缓存 */
} app_t;
```

字段管理者：

| 字段 | 管理者 | 说明 |
|------|--------|------|
| `app_data` | App 自身 | open 时分配，close 时释放 |
| `user_data` | 调用方 | appcmd 输入 / 输出的二进制数据指针 |
| `output_fn/output_ctx` | 调用方 / terminal | 流式输出回调 |
| `callback` | 注册方 | 事件通知（中断或 App 内部） |
| `callback_data` | App 内部 | 持久句柄（文件 fd / tile handle） |
| `mode_data` | App 内部 | 模式相关缓存 |
| `app0..app3` | `appget` 自动 | 由 `app_dep_str` 解析填充 |

### `app_output_fn` — 流式输出回调

```c
typedef int (*app_output_fn)(const void* data, uint32_t len, void* ctx);
```

输出类命令（`cat`/`ls`/`read`/`info` 等）当 `user_data` 为 NULL 但 `output_fn` 非 NULL 时，会被分块调用此回调。

## 注册宏

```c
#define REGISTER_APP(name, dep, ops, desc)
#define REGISTER_APP_EX(name, dep, app_dep, ops, desc)
```

| 参数 | 类型 | 内容 |
|------|------|------|
| `name` | 字符串 | 注册名（运行期 `appget` 查找键），如 `"gpio_port"` |
| `dep` | 字符串 | pdrv 依赖，**已废除，固定填 `"0"`** |
| `app_dep` | 字符串 | App 之间依赖，格式 `"N\0name1\0name2\0..."`，无依赖时 `REGISTER_APP` 简写为 NULL |
| `ops` | `const papp_ops_t*` | 5 回调表 |
| `desc` | 字符串 | 仅文档作用，编译期不存储 |

展开为两个 static const 符号 + 一个放 `app_table` 段的 `papp_t` 实例（`used` 属性防 LTO 丢弃）。

### `app_dep_str` 格式

```
"1\0gpio_port"                  → 1 个依赖 → app0 = gpio_port
"2\0KSCGUI\0button16"            → 2 个依赖 → app0=KSCGUI, app1=button16
"4\0a\0b\0c\0d"                 → 4 个依赖 (上限)
```

- 第一字符为 ASCII 数字 `N`（`'1'`~`'4'`），依赖数 `> 4` 会被截到 4。
- 紧跟一个 `\0`，之后是 `\0` 分隔的 App 名，每个名字为普通 C 字符串。
- 被依赖者在 `__start_papp_table` 中查不到时，整体 `appget` 失败（返回 NULL）。
- 注意：源码字面量写作 `"2\0KSCGUI\0button16"`，编译器会把它合并为单条字符串（包含嵌入式 `\0`），**不要**用 `'2'+'\0'+"..."` 拼接写法。

### 链接器要求

`REGISTER_APP` 文件 **必须**加入**目标可执行文件**的 `target_sources(... PRIVATE ...)` 中，而不能进 `add_library(...)`：

```cmake
# apps/CMakeLists.txt
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gpio_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/uart_serial.c
    # ...
)
```

否则链接器（`--gc-sections` + LTO）会把 orphan section `app_table` 段连同 `papp_t` 符号一起丢弃，`appget` 始终返回 NULL。

## 公共 API

### `appget` — 取得（或创建）App 实例

```c
app_t* appget(const char* name);
```

1. 在 `_app_cache` 链表中按 `name` 查找；命中 → `get_refs++`，返回缓存实例。
2. 未命中 → 在 `__start_papp_table[]` 中找 `papp_t`，`osmalloc(sizeof(app_t))` 新建，递归 `appget` 解析 `app_dep_str` 填入 `app0..app3`，插入缓存链表。
3. 找不到 → 返回 NULL。

**幂等但不重入**：内部读全局缓存链表，未加锁。可被多线程不存在（STM32 单 main loop + 中断）。

### `appopen` — 打开 App（分配内部上下文）

```c
int appopen(app_t* app);
```

- 幂等：第二次调用直接返回 0，不再调 `ops->open`。
- 成功调 `ops->open`，约定由 App 在此 `osmalloc` 出 `app->app_data`。
- 未找到缓存节点 / 无 `ops->open` → 返回 `-1`。

### `appclose` — 关闭 App（释放内部上下文）

```c
int appclose(app_t* app);
```

- **没有引用计数**。一旦调用 `ops->close`，`APP_STATE_OPENED` 清零，`app_data` 被释放。
- **共享者立即失效**：多块代码共享同一 App 时，A 调 `appclose` 会让 B 持有的 `app_t*` 内部上下文被释放，B 再调任何 API 行为未定义。
- 共享 App 的协作约定：要么由最终所有者统一 close；要么 idle-loop 持有的 App（terminal 等）永远不 close。
- 已关闭状态下再 close 返回 0（幂等）。

### `appread` — 读路径

```c
int appread(app_t* app, void* data, uint32_t count, uint32_t mode);
```

- `mode` 通常以 `(inst << 4) | op` 编码硬件实例与操作，具体语义见各 App。
- 返回字节数 / 状态码 / 0；`<0` 错误。
- 对带环形缓冲的 App（uart_serial RX），`mode` 高位 bit6=阻塞、bit7=读溢出计数，详见 [`apps.md`](apps.md)。

### `appwrite` — 写路径

```c
int appwrite(app_t* app, void* data, uint32_t count, uint32_t mode);
```

- `mode` 同上。
- 常见用法：发送字节 / 配置 GPIO / 触发 SPI 帧。`data` 为 NULL 表示纯触发（如启动定时器）。

### `appcmd` — 字符串命令接口

```c
int appcmd(app_t* app, const char* cmdline);
```

详见 [`appcmd.md`](appcmd.md)。

### `appcmd_argv` — 已解析参数直接调用

```c
int appcmd_argv(app_t* app, const char* cmdname, const char** argv);
```

跳过字符串解析，直接传入 `cmdname` 与长度为 26 的 `argv[26]`（未使用槽位为 NULL）。用于：
- 中断上下文（`appcmd` 用了 `static char buf[128]`，不可重入）
- 程序生成的命令组合（避免反向字符串化）

### `appfree` — 释放引用

```c
void appfree(app_t* app);
```

- `get_refs--`；当 `get_refs == 0` 时**应**从链表移除并 `osfree(app)`。
- **当前实现未真正释放**（标记为 TODO），仅递减计数。
- 调用 `appfree` 不会触发 `ops->close`，请先 `appclose`。

## 单例与引用计数

```
                            ┌──────────────┐
        appget("gpio_port") │ _app_cache    │ 缓存单向链表
         ┌──────────────────►│ {app*, get_refs,}│
         │                   │ {app*, get_refs,}│
         │ 不命中             └──────────────┘
         ▼
  __start_papp_table[]  →  osmalloc(app_t)
  遍历查找 papp_t        →  resolve app_dep_str
                          →  app0..app3 = appget(name_j)
                          →  链表头插入 (get_refs=1)
```

- 每个 `appget(name)` 命中缓存时 `get_refs++`，新创建时 `get_refs=1`。
- `appfree` `get_refs--`，理论 `==0` 时可真正析构（暂未实现）。
- `appopen` 由 `APP_STATE_OPENED` 位锁定，幂等；`appclose` 清该位，**立即生效**。

## 扩展指南：编写新 App

最小完整骨架见模块根 `README.md` "扩展指南"。要点：

1. 每个 `.c` 文件写一个 `REGISTER_APP_EX`（不要在一个文件里写多个，LTO 段排序不可控）。
2. 内部上下文类型放 `static` 定义，`open` 中 `osmalloc(sizeof(my_ctx_t))` 赋给 `app->app_data`；**禁止用 static / 全局数组承载 per-instance 数据**（栈只 1KB，全局静态会被多实例共享，破坏单例隔离）。
3. `close` 中 `osfree(app->app_data)` 并置 NULL，使后续 `appread/appwrite/appcmd` 能短路返回。
4. `appcmd` 派发推荐用静态 `const {name, handler}[]` 分发表（参见 `gpio_cmds[]`、`tim_cmds[]`、`list_cmd_entry_t` 等模式）。

### 常见错误

- **静态 buffer 不可重入**：`appcmd` 用 `static char buf[128]`，所以不允许在中断里调 `appcmd`。中断里要么调 `appcmd_argv`，要么直接 `appwrite`。
- **`appclose` 与共享**：A `appclose(uart)` 之后，B 持有的 `uart*` 在 `ops->open` 之前不可用；要么各自 `appopen` 后协调，要么不 close。
- **链接器丢段**：`REGISTER_APP` 文件进 `add_library` 会让 `app_table` 段被丢弃，`appget` 全部失败。务必进 `target_sources`。
- **`app_dep_str` 写成多个并列字符串**：`"2" "\0KSCGUI" "\0button16"` 在 GCC 是相邻字面量拼接，与 `"2\0KSCGUI\0button16"` 等价；但写成 `'2'` + `0` + `"KSCGUI"` 这种 char/str 混合会编译错。永远用单条字符串字面量。