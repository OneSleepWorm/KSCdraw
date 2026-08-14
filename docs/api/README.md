# KSCOS API 文档

本目录是 KSCOS 框架与应用的**接口契约**文档（不是教程，也不是源码注释拷贝）。
面向两类读者：

1. **框架使用者** —— 想用 C 调用 `appget/appopen/appcmd` 把某个已有 App 跑起来。
2. **框架扩展者** —— 想自己 `REGISTER_APP` 写一个新模块并接入 `appcmd` 协议。

## 文档地图

| 文档 | 内容 | 对应头文件 |
|------|------|-----------|
| [`app_framework.md`](app_framework.md) | `papp_t` / `app_t` 数据结构、`REGISTER_APP` 宏、`appget/appopen/appclose/appread/appwrite/appfree` 全套生命周期 API、单例缓存与引用计数语义 | `inc/app.h` |
| [`appcmd.md`](appcmd.md) | `appcmd` 字符串命令接口规范：语法、引号转义、26 槽位、宏 `APPCMD_HAS/APPCMD_ARG`、数据通道 (`user_data`/`output_fn`/`callback_data`)、返回值约定、mode 字节编码 | `inc/app.h` (运行期由 `src/app.c` 实现) |
| [`system.md`](system.md) | `sys_init` / `kscprintf` / **固定地址 app (SYSTEMAPP 内核服务 + CONSOLEAPP 全局路由)** / **内存池统计** / **fastsystem.h 内联宏** / 错误处理器 — 以及"1KB 栈"约束 | `inc/KSCOSsystem.h` + `inc/kscsystem.h` + `inc/mempool.h` |
| [`kscdraw.md`](kscdraw.md) | `k_draw_device` 抽象、`KSC_window`、`ksc_obj_t` + 扩展 `draw_table`、全部基本绘图函数、颜色常量 | `inc/KSCdraw.h` |
| [`apps.md`](apps.md) | 各 App 的 appcmd 命令表与 `appread/appwrite` mode 表 (gpio_port / uart_serial / tim_clock / button16 / super_spi / KSCGUI / list / ctrl_list / snake / w25qxx_base / littlefs / terminal / open) | `apps/*.c` + `apps/app_config.h` |

## 阅读约定

- 函数原型使用 K&R 风格签名，与 `inc/*.h` 一致。
- 参数类型 `void*` 表示 `app->user_data` 之类的二进制缓冲，由调用方负责生命周期。
- 返回值：未特别说明时，`≥0` 表示成功（语义随命令），`<0` 表示错误。
- mode 字节编码统一为 `(inst << 4) | op`，详见 [`appcmd.md` § mode 字节编码](appcmd.md#mode-字节编码)。
- 文档中所述"持久句柄"指存于 `app->callback_data`，生命周期跨多次 appcmd 调用，需显式释放。

## 版本约定

- 与 `inc/app.h` / `apps/*.c` 当前 `main` 分支同步。
- 命令清单如有出入，**以源码中的 `*_cmds[]` 分发表为准**；本文档跟随源码更新。

## 相关文档

- `docs/KSCGUI_API.md` —— KSCGUI 的早期命令参考（部分内容已过时，新内容并入本目录的 `apps.md`）。
- `README.md`（本模块根目录）—— KSCOS 概览、目录结构、平台开关、应用清单、构建说明。