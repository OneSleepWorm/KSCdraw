# KSCOS Tools

`KSCOS/tools/` 目录持有 AI agent / 开发者与下位机（PC 模拟或 STM32 真机）通信
用的辅助脚本。

## 目录布局

```
tools/
├── README.md              本文档
├── transfer.py             XMODEM-128 文件上传客户端
├── monitor/                daemon + CLI 客户端（IPC 通信框架）
│   ├── __init__.py         包入口，导出 main()
│   ├── __main__.py         `python monitor/ <cmd>` 直接运行时入口
│   ├── cli.py              argparse + 命令分发
│   ├── client.py           _send_to_daemon / _ensure_daemon / _client_run
│   ├── daemon.py           Daemon 类 + PID 管理 + log_entry + run_daemon
│   ├── transport.py        Transport 抽象 + FileTransport + SerialTransport + MockTransport
│   └── tests/              pytest 单元测试（50 个用例）
└── logs/                   daemon 运行日志（自动生成）
    ├── serial.log          完整收发记录（含时间戳、tag、hex）
    ├── serial_user.log     仅下位机主动输出（去协议头、去转义）
    └── daemon.pid          daemon 单例锁（运行时存在，退出时清理）
```

## monitor 使用手册

### 概览

`monitor` 是 KSCOS 与下位机通信的中间层，采用 **客户端 + 守护进程 (daemon)** 架构：

```
┌──────────┐      TCP JSON       ┌──────────┐  FileTransport / SerialTransport  ┌──────────┐
│  Client  │  ────────────────  │  Daemon  │  ────────────────────────────────  │  KSCOS   │
│ (CLI一次性)│  ←───────────────  │ (后台常驻)│  ←───────────────────────────────  │ (下位机)  │
└──────────┘      响应 JSON       └──────────┘     stdin.txt ← / → stdout.txt    └──────────┘
```

- 客户端进程发一条 JSON 命令 → daemon 处理 → 通过 transport 投递到下位机。
- daemon 后台常驻，每条命令重置空闲计时器（默认 60s），可手动 `close`。
- daemon 用 **PID 文件单例锁**，僵尸进程会被自动检测 + kill + 重启。

### 传输层

| transport | 后端 | 用途 | 后台 reader |
|---|---|---|---|
| `file` (默认) | 本地文件 IO (`.data/stdin.txt` & `stdout.txt`) | PC 端与 `KSCOS.exe` 通信 | 否 |
| `serial` | pyserial | 真机与 STM32 通信 | 是（捕获 MCU 自发数据） |
| `mock` | 内存 `bytearray` | 单元测试 | 否 |

### 命令一览

```powershell
# 启动守护进程
python KSCOS/tools/monitor open                        # 默认 file 模式
python KSCOS/tools/monitor open --transport serial --port COM6 --baud 115200
python KSCOS/tools/monitor open --idle 30                # 自定义空闲超时（秒）

# 连通测试
python KSCOS/tools/monitor ping

# 发命令到下位机（write 自动追加 \r\n；KSCOS appcmd 必须用 `-x value` 标志语法）
python KSCOS/tools/monitor write "littlefs mount"
python KSCOS/tools/monitor write "littlefs echo -m Hello -p /note.txt"   # 多 flag 用空格分隔
python KSCOS/tools/monitor write --noeol "AT"             # 不追加 \r\n
python KSCOS/tools/monitor write --hex "6c6974746c656673206c73"  # 输入 hex

# 发命令并等待回应（--expect 可选；省略则收数据静默 200ms 后返回）
python KSCOS/tools/monitor exchange "littlefs ls -p /" --timeout 3        # 无 expect，静默期退出
python KSCOS/tools/monitor exchange "littlefs echo -m Probe" --expect Probe --timeout 3  # 匹配即返回
python KSCOS/tools/monitor exchange --hex "4154" --expect "4f4b" --timeout 2  # hex 输入/hex 匹配

# 实时流式显示下位机输出（Ctrl+C 停止，或 --timeout N 自动退出）
python KSCOS/tools/monitor monitor
python KSCOS/tools/monitor monitor --timeout 4          # 4 秒后自动退出

# 关闭 daemon（同时清空 stdin.txt + stdout.txt）
python KSCOS/tools/monitor close
python KSCOS/tools/monitor stop           # 别名
```

### 响应字段

所有命令回 JSON：

| 字段 | 类型 | 说明 |
|---|---|---|
| `status` | str | `"ok"` / `"error"` / `"timeout"` |
| `msg` | str | `ping`/`open`/`close` 的状态描述 |
| `sent` | int | `write` / `exchange` 实际写入下位机的字节数 |
| `received` | str | `exchange` 累积收到的回应（已 decode，或 hex 模式下的 hex 串） |
| `matched` | str | `--expect` 命中的子串（空字符串表示未命中） |
| `length` | int | `received` 的长度 |
| `error` | str | 错误描述 |

### 下位机（KSCOS）协议要点 ← 重点

#### File 模式数据流

```
client ─TCP→ daemon ─append→ stdin.txt ←─read+truncate─ KSCOS
client ←TCP─ daemon ←─read-from-offset─ stdout.txt ←─append─ KSCOS
```

**路径**（全部锚定 monitor 包位置，与 CWD 无关）：

| 文件 | 用途 |
|---|---|
| `KSCOS/.data/stdin.txt` | daemon → KSCOS 命令流（daemon append，KSCOS read + truncate） |
| `KSCOS/.data/stdout.txt` | KSCOS → daemon 输出流（KSCOS append，daemon 读偏移跟到文件尾） |
| `KSCOS/.data/flash.bin` | littlefs 持久镜像（跨会话保留，**不**随 daemon close 清空） |

> **⚠️ 严禁删除或清空 `.data/` 下的任何文件。** `flash.bin` 是用户数据持久存储，删除 = 丢失所有文件。

#### KSCOS 命令格式（appcmd）

KSCOS 的命令解析器要求严格的 `-x value` 标志语法：

- ✅ `littlefs echo -m HelloWorld`
- ✅ `littlefs echo -m Hello -p /note.txt`
- ❌ `littlefs ls /` — 裸参会触发 `appcmd: unknown arg '/' (use -p for path?)` 警告
- ✅ `littlefs ls -p /`

多个 flag 用空格分隔即可，**不**支持引号包裹多词值（`-m "Hello World"` 会被解析成 `-m Hello`，余下 `World` 触发 unknown arg 警告）。

#### 启动时序

**推荐顺序：daemon 先 → KSCOS 后。** 原因：

| 顺序 | 结果 |
|---|---|
| daemon 先 → KSCOS 后 | KSCOS 把 boot 输出 ("term start\nterm end\n") 写到 stdout.txt，daemon 把读偏移对到当前文件尾，**保留** boot 输出 |
| KSCOS 先 → daemon 后 | daemon 启动时把读偏移对到当前文件尾（指向 boot 输出之后），boot 内容仍在 stdout.txt 但 daemon **不会回读**，需直接 `Get-Content` 读文件查看 |
| `close` 后 | stdin.txt + stdout.txt 同时被清空（flash.bin 保留） |

**完整首次流程**（首次使用 + 已 mount 过文件系统）：

```powershell
# 1. 启动 daemon
python KSCOS/tools/monitor open

# 2. 启动 KSCOS（boot 输出会出现在 stdout.txt）
Start-Process -WindowStyle Hidden E:\CProject\KSCcomputer\KSCOS\build_ninja\KSCOS.exe
Start-Sleep 3

# 3. 首次格式化（只需一次；之后文件系统持久在 flash.bin）
python KSCOS/tools/monitor write "littlefs format"

# 4. 每次启动后必须 mount
python KSCOS/tools/monitor write "littlefs mount"

# 5. 操作文件
python KSCOS/tools/monitor write "littlefs echo -m Hello -p /note.txt"
python KSCOS/tools/monitor write "littlefs cat -p /note.txt"
python KSCOS/tools/monitor write "littlefs ls -p /"

# 6. 关闭
python KSCOS/tools/monitor close
```

#### exchange 匹配机制

`--expect STR` 实行**累积匹配**（非逐行）：

- daemon 发完命令后立刻轮询 transport，把读到的新字节解码后**追加**到 accumulated buffer
- 一旦 buffer 中**包含** `expect_str` 子串就立即返回 `{"status":"ok","matched":STR}`
- 超时未匹配返回 `{"status":"timeout","received":..., "matched":""}`

注意：`received` 字段是 hex 模式下的 hex 串（如 `"4865780d0a"`），`--expect` 也要按 hex 输入（如 `"486578"`），不要用空格分隔。

### 日志

| 文件 | 内容 |
|---|---|
| `tools/logs/serial.log` | 完整收发记录：`[时间戳][方向(tag)] 内容`。方向 = `发` / `收` / `系统`；tag = `TXT` / `HEX` / 空字符串 |
| `tools/logs/serial_user.log` | 仅下位机主动输出（不含协议头、不含 send 数据、去 `\r` 转义、去前导空行），适合直接看 |
| `tools/logs/daemon.pid` | daemon 子进程 PID；daemon 启动写入、退出/被 kill/异常时清理 |

⚠️ **daemon 重启会清空 serial.log + serial_user.log**（daemon 子进程启动时 `open(... "w")`）。需要完整历史日志时保持 daemon 不重启。

### PID 文件机制（僵尸 daemon 自动恢复）

- daemon 启动时把自己 PID 写到 `logs/daemon.pid`；atexit + finally 兜底清理。
- **任何客户端命令**调用 `_ensure_daemon` 时执行健康检查：
  1. `ping` 通 → 复用现有 daemon
  2. ping 失败 + PID 文件存在 + PID 还活着但端口拒绝 = **僵尸 daemon** → `taskkill /F` 强杀 → 清 PID 文件 → 启动新 daemon
  3. ping 失败 + PID 文件不存在 / PID 已死 → 直接启动新 daemon
- 用户也可以手动 `Stop-Process -Id <PID> -Force`：下次 `ping`/`write`/...会自动检测死 PID 并重启。

### 单元测试

```powershell
cd KSCOS/tools/monitor
python -m pytest tests/ -v          # 50 个用例，约 8 秒
```

测试用 `MockTransport`（内存 transport），**不**开 socket、不动文件、不依赖 pyserial。
覆盖率：Transport / FileTransport / MockTransport / log_entry / PID 文件生命周期 /
Daemon 全部协议分支 / `_send_to_daemon` 异常路径 / `_ensure_daemon` 僵尸恢复。

---

## transfer.py

XMODEM-128 文件上传客户端。独立于 monitor 框架。

```powershell
python KSCOS/tools/transfer.py send <local_path> <remote_path>
```

具体用法参考 `transfer.py --help`。