# Dispatcher 示例

本目录包含一个端到端示例，演示 `EventDispatcher`（服务端）与 `EventReceiver`（客户端）的完整使用流程。

## 构建依赖

- CMake >= 3.10
- C11 编译器
- glib-2.0 开发包

## 构建方式

### 方式一：独立构建

```bash
cmake -S src/dispatcher -B build -DBUILD_DISPATCHER_EXAMPLE=ON
cmake --build build --target example_dispatch
```

### 方式二：随测试一同构建

```bash
cmake -S src/dispatcher -B build -DBUILD_DISPATCHER_EXAMPLE=ON -DENABLE_TESTING=ON
cmake --build build
```

## 运行

```bash
./build/example/example_dispatch
```

### 预期输出

```
[server] dispatcher listening on /tmp/dispatcher_example.sock
[client] connected to dispatcher
[server] accepted client connection
[server] sent event: action=42 path=/home/user/example.txt
[client] received event: action=42 path=/home/user/example.txt
example completed successfully
```

## 示例说明

程序运行后会在同一进程中完成以下流程：

1. 创建 `EventDispatcher`，监听 Unix Domain Socket `/tmp/dispatcher_example.sock`
2. fork 子进程，子进程创建 `EventReceiver` 连接到 dispatcher
3. 父进程调用 `event_dispatcher_accept()` 接受连接
4. 父进程调用 `event_dispatcher_send()` 发送一条文件事件
5. 子进程调用 `event_receiver_receive()` 接收并校验事件
6. 双方释放资源，程序退出

示例使用的订阅配置为默认前缀 `"/`，即所有已连接客户端都会收到任意路径的事件。实际使用时可通过 `dispatch_config_t` 中的 `get_user_subscribed` 回调函数按用户 UID 动态配置路径前缀过滤。
