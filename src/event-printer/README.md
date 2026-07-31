# event-printer（内核事件测试接收端）

本子项目是一个**直接读取内核模块事件**的测试工具，用于验证
`src/kernelmod` 是否正确发送 VFS 事件。它**不**经过
`deepin-anything-server` 或 dispatcher，而是直接消费内核模块产生的事件。

## 三种传输方式

启动时读取 `/sys/kernel/vfs_monitor/transport` 的值自动选择传输方式，
也可用 `--transport=` 强制覆盖：

| transport 值 | 接收方式 | 数据来源 |
|--------------|----------|----------|
| `mmap` | open + poll + `read()` 字符设备 | `/dev/vfs_monitor`、`/dev/vfs_monitor_proc` |
| `mmap-ring` | open + `mmap()` 共享内存，零拷贝直接读 slot | `/dev/vfs_monitor`、`/dev/vfs_monitor_proc` |
| `genl` | 加入 generic netlink 多播组 | `vfsmonitor` family 的 `vfsmonitor_de`、`vfsmonitor_pi` 组 |

### mmap 传输（read 路径）

内核模块通过 misc 字符设备暴露两个无锁 SPSC ring buffer：
- `/dev/vfs_monitor` — dentry 事件（`vfs_ringbuf_dentry_rec`）
- `/dev/vfs_monitor_proc` — 进程信息（`vfs_ringbuf_proc_rec`）

每次 `read()` 返回一条完整记录（`record_size` 字节）。

### mmap-ring 传输（零拷贝路径）

`mmap-ring` 将内核 ring buffer 页面直接映射到用户空间，消费者从
映射内存中读取记录，无需 `read()` 系统调用。消费完成后以
`__atomic_store_n(..., __ATOMIC_RELEASE)` 推进 `consumer_tail`，
与内核的 `smp_load_acquire` 配对。

> **SPSC 约束**：内核在 `open()` 时以 `atomic_cmpxchg` 保证每个通道
> 同一时刻只有一个 fd 被打开。`mmap-ring` 与 `mmap`（read）互斥，
> 不能同时使用同一设备。

### genl 传输

内核模块通过 generic netlink 多播两组消息：
- `VFSMONITOR_C_NOTIFY`（dentry 通道，组 `vfsmonitor_de`）
- `VFSMONITOR_C_NOTIFY_PROCESS_INFO`（进程通道，组 `vfsmonitor_pi`）

## 与 server/example/example_receiver 的区别

| 维度 | example_receiver | event-printer |
|------|------------------|---------------|
| 数据来源 | server 的 Unix Domain Socket | 内核模块（mmap / mmap-ring / genl） |
| 传输 | SOCK_SEQPACKET | mmap ring buffer / generic netlink |
| 依赖 | deepin-anything-dispatcher 库 | glib + libnl |
| 用途 | 验证 server→下游分发链路 | 验证 kernelmod 事件发送本身 |

## 架构

```
main 线程
─────────────────────────────────────────────
 读取 /sys/kernel/vfs_monitor/transport
 根据 transport 创建 receiver:
   mmap      → ep_mmap_receiver_new()      (g_unix_fd_add × 2)
   mmap-ring → ep_mmap_ring_receiver_new() (g_unix_fd_add × 2)
   genl      → ep_genl_receiver_new()      (g_unix_fd_add × 1)
 g_unix_signal_add(SIGINT/SIGTERM)
 g_main_loop_run()
   ↑ fd 可读 → drain → on_event → ep_event_print
   │ 信号 → quit
   ▼
 free receiver (打印统计)
```

所有 fd 监听通过 `g_unix_fd_add` 挂在主线程默认 `GMainContext` 上，
信号处理通过 `g_unix_signal_add`，无需手动管理线程。

## 构建依赖

- CMake >= 3.10
- C 编译器（C11）
- glib-2.0 开发包（含 `glib-unix.h`，由 `gio-unix-2.0` 提供）
- libnl-3.0 + libnl-genl-3.0（genl 传输所需）

## 构建方式

随全项目构建即可，已默认接入 `src/CMakeLists.txt`：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target deepin-anything-event-printer
```

## 运行

1. 确认 `vfs_monitor` 内核模块已加载：
   ```bash
   cat /sys/kernel/vfs_monitor/transport   # 显示 mmap 或 genl
   ```
2. 运行（transport 自动检测）：
   ```bash
   # mmap / mmap-ring 模式需要 root（设备节点 0660）
   # genl 模式需要 root（多播组绑定限制为 CAP_SYS_ADMIN）
   sudo ./build/src/event-printer/deepin-anything-event-printer
   ```
3. 在被监控的文件系统上做些改动（新建/删除/重命名），即可看到事件输出：
   ```
   NEW_FILE     8:1 /home/user/笔记.txt
   RENAME_FROM  8:1 cookie=12345 /home/user/old.txt
   RENAME_TO    8:1 cookie=12345 /home/user/new.txt
   PROC         cookie=12345 uid=1000 tgid=1234 /home/user/new.txt
   ```
4. `Ctrl+C` 退出，会打印统计：
   ```
   Received termination signal, shutting down...
   Total events received: 4
   dentry events: 3
   proc events:   1
   event-printer shutdown complete
   ```

### 命令行选项

```
Usage: deepin-anything-event-printer [OPTIONS]

Receive and print VFS events from the kernel module.
Transport is auto-detected from /sys/kernel/vfs_monitor/transport.

Options:
  --dentry=PATH   Dentry device (mmap only, default: /dev/vfs_monitor)
  --proc=PATH     Process device (mmap only, default: /dev/vfs_monitor_proc)
  --transport=T   Force transport: mmap, mmap-ring, or genl (overrides sysfs)
  -h, --help      Show this help and exit
```

## 许可

GPL-3.0-or-later（见源文件 SPDX 头）。
