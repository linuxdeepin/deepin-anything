# Server 接收端示例（example_receiver）

本示例是一个**真实可用的接收端（客户端）程序**，演示下游组件如何连接到正在运行的
`deepin-anything-server`，并消费其通过 Unix Domain Socket 分发出来的 VFS 文件事件。

它刻意贴近生产级集成方式，而不是最小化的 API demo。

## 展示的真实使用场景

| 场景 | 实现方式 |
|------|----------|
| 独立线程 + GMainLoop | 接收逻辑运行在独立的 `event_receiver` 工作线程中，线程内创建**私有的 `GMainContext`** 并 `push_thread_default`，再创建 `GMainLoop`。fd 监听源因此挂在本线程的私有上下文上，而不会错误地附着到主线程的默认上下文（参见 `src/server/event-dispatcher.c` 中关于 `g_unix_fd_add_full` 与线程默认上下文的说明）。 |
| 非阻塞 fd 监听 | 通过 `event_receiver_get_socket()` 取得已连接 socket fd，用 `g_unix_fd_add_full()` 监听 `G_IO_IN \| G_IO_HUP \| G_IO_ERR`；fd 可读时回调内调用 `event_receiver_receive()` 取出一条完整消息（SOCK_SEQPACKET 保边界，单次 `recv()` 即完整事件）。 |
| 动作码解码与可读输出 | 把 `vfs_change_consts.h` 中的 `ACT_NEW_FILE` / `ACT_DEL_FILE` / `ACT_RENAME_FROM` / `ACT_RENAME_TO` / `ACT_MOUNT` 等翻译为可读字符串，按 `动作 路径` 一行打印；未知动作码以 `UNKNOWN(编号)` 形式打印且不崩溃。 |
| 信号优雅退出 | 主线程用 `g_unix_signal_add()` 注册 `SIGINT` / `SIGTERM`，收到信号后退出主 `GMainLoop`，进而请求工作线程停止并 `g_thread_join()` 等待其清理完毕，最后打印事件总数。 |

> 说明：server 端把 rename 拆成两条事件分别下发（`ACT_RENAME_FROM_*` 与
> `ACT_RENAME_TO_*`，见 `src/server/event-dispatcher.c` 的 `process_event`），
> 且 `dispatch_event_t` 不携带 cookie，因此本示例按两条独立事件分别打印
> `RENAME_FROM` / `RENAME_TO`，由观察者自行对应。

## 架构

```
main 线程                          event_receiver 工作线程
─────────────                      ─────────────────────────
 parse args                         g_main_context_new()        (私有上下文)
 g_unix_signal_add(SIGINT/SIGTERM)   push_thread_default()
 g_thread_new(...) ───────────────►  event_receiver_new(socket)  (connect)
 g_main_loop_run(main_loop)         g_unix_fd_add_full(fd)       (非阻塞监听)
   ↑ 仅处理信号                     g_main_loop_run(worker_loop)
   │ 信号 → quit(main_loop)              ↑ fd 可读 → event_receiver_receive()
   ▼                                     │   → 解码动作码 → g_print 事件
 quit(worker_loop)                      │ HUP/断连 → worker_request_shutdown
 g_thread_join(worker) ◄────────────────┘
 打印统计 + free
```

工作线程的所有退出路径（连接失败、断连、信号触发）都会调用
`g_main_loop_quit(main_loop)`，保证主线程能被唤醒并干净收尾。

## 构建依赖

- CMake >= 3.1
- C 编译器（C11）
- glib-2.0 开发包（含 `glib-unix.h`）
- `deepin-anything-dispatcher` 静态库（由 `src/dispatcher` 提供，随全项目构建）

## 构建方式

本示例默认**不构建**，需开启 `BUILD_SERVER_EXAMPLE`。由于 `src/server` 依赖
`deepin-anything-dispatcher` 目标，请在仓库根目录进行全项目配置：

```bash
cmake -S . -B build -DBUILD_SERVER_EXAMPLE=ON
cmake --build build --target example_receiver
```

仅构建 dispatcher 示例（与本项目无关）时另见 `src/dispatcher/example/README.md`。

## 运行

1. 确保 `deepin-anything-server` 已运行（其 socket 默认位于
   `/run/deepin-anything/event-dispatcher.sock`，且 server 默认按 UID 订阅
   `$HOME/` 前缀，见 `src/server/event-dispatcher.c`）。
2. 运行示例：

   ```bash
   # 普通用户即可（订阅基于 SO_PEERCRED 取得的真实 UID）
   ./build/src/server/example/example_receiver
   ```

3. 在你的 home 目录下做一些文件改动（新建/删除/重命名），即可看到事件输出：

   ```
   NEW_FILE     /home/user/笔记.txt
   RENAME_FROM  /home/user/old.txt
   RENAME_TO    /home/user/new.txt
   DEL_FILE     /home/user/笔记.txt
   ```

4. `Ctrl+C` 退出，会打印统计：

   ```
   Received termination signal, shutting down...
   Total events received: 3
   example_receiver shutdown complete
   ```

### 自定义 socket 路径

```bash
./example_receiver --socket=/tmp/my-dispatcher.sock
```

### server 未运行时

连接失败会打印清晰提示并以非零码退出：

```
** (process:xxxxx): 17:00:00.000: CRITICAL: Failed to connect to dispatcher at /run/deepin-anything/event-dispatcher.sock
** (process:xxxxx): 17:00:00.000: MESSAGE: Is deepin-anything-server running and reachable?
```

## 扩展点

本示例为保持清晰只实现核心场景，以下为可扩展方向：

- **断线自动重连**：在 `on_fd_readable` 的断连分支重新 `event_receiver_new()` 并重建 fd 源，可加指数退避定时器。
- **客户端路径过滤**：在 `print_event` 前用 `g_str_has_prefix(event->event_path, prefix)` 过滤只关注目录。
- **事件统计/聚合**：用一个 `GHashTable<动作, 计数>` 在退出时打印各动作计数。
- **多路复用**：用 `event_receiver_get_socket()` + `poll()`/`epoll` 把多个 receiver 合并到一个事件循环。

## 许可

GPL-3.0-or-later（见源文件 SPDX 头）。
