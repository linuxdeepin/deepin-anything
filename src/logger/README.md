# deepin-anything-logger

deepin-anything-logger 是深度文件系统事件监控和日志记录服务，作为 deepin-anything 项目的核心组件之一，负责实时监听文件系统操作事件并记录到日志文件中。

## 项目概述

### 功能特性

- **实时文件系统事件监听**: 通过内核模块和 netlink 接口监听文件系统操作
- **可配置事件过滤**: 支持选择性记录特定类型的文件操作事件
- **智能日志轮转**: 自动压缩和管理历史日志文件
- **CSV格式日志**: 结构化的事件记录格式，便于后续分析
- **动态配置管理**: 支持通过 dconfig 动态调整配置参数
- **系统服务集成**: 作为 systemd 服务运行，开机自启动

### 技术架构

- **编程语言**: C语言 (C11标准)
- **核心框架**: GLib 2.0, GIO
- **通信机制**: Netlink Generic (libnl-3.0, libnl-genl-3.0)
- **配置管理**: dconfig (deepin配置管理系统)
- **服务管理**: systemd
- **运行权限**: root用户

## 支持的事件类型

logger服务能够监听并记录以下文件系统事件：

| 事件类型 | 描述 | 配置标识 |
|---------|------|----------|
| 文件创建 | 新文件被创建 | `file-created` |
| 文件删除 | 文件被删除 | `file-deleted` |
| 文件夹创建 | 新目录被创建 | `folder-created` |
| 文件夹删除 | 目录被删除 | `folder-deleted` |
| 文件重命名 | 文件重命名或移动 | `file-renamed` |
| 文件夹重命名 | 目录重命名或移动 | `folder-renamed` |
| 链接创建 | 硬链接被创建 | `link-created` |
| 符号链接创建 | 软链接被创建 | `symlink-created` |

## 配置参数

logger服务通过dconfig系统进行配置管理，配置文件为 `org.deepin.anything.logger`：

### 配置项详解

| 配置项 | 类型 | 默认值 | 说明 |
|-------|------|--------|------|
| `log_events` | boolean | `true` | 是否启用事件记录功能 |
| `log_events_type` | string[] | `["file-deleted", "folder-deleted"]` | 需要记录的事件类型列表 |
| `log_file_count` | integer | `10` | 保留的历史日志文件数量 |
| `log_file_size` | integer | `1` | 单个日志文件最大大小(MiB) |
| `print_debug_log` | boolean | `false` | 是否启用调试日志输出 |

### 配置修改

可以通过以下方式修改配置：

```bash
# 查看当前配置
dconf read /org/deepin/anything/logger/log_events

# 修改事件类型配置
dconf write /org/deepin/anything/logger/log_events_type '["file-created", "file-deleted", "folder-created", "folder-deleted"]'

# 修改日志文件大小
dconf write /org/deepin/anything/logger/log_file_size '5'

# 启用调试日志
dconf write /org/deepin/anything/logger/print_debug_log 'true'
```

## 日志格式

事件日志以CSV格式存储在 `/var/log/deepin/deepin-anything-logger/events.csv`，包含以下字段：

```csv
时间戳,进程路径,用户ID,进程ID,事件类型,文件路径
```

### 日志示例

```csv
2024-01-15 10:30:25.123,/usr/bin/touch,1000,12345,file-created,/home/user/test.txt
2024-01-15 10:30:30.456,/bin/rm,1000,12346,file-deleted,/home/user/test.txt
2024-01-15 10:31:00.789,/usr/bin/mkdir,1000,12347,folder-created,/home/user/newfolder
```

### 字段说明

- **时间戳**: 事件发生的精确时间 (格式: YYYY-MM-DD HH:MM:SS.mmm)
- **进程路径**: 触发事件的进程可执行文件路径
- **用户ID**: 执行操作的用户UID
- **进程ID**: 触发事件的进程PID
- **事件类型**: 文件系统操作类型
- **文件路径**: 被操作的文件或目录的完整路径

## 系统架构

### 核心组件

1. **EventListener (事件监听器)**
   - 通过netlink接口与内核模块通信
   - 接收文件系统事件通知
   - 根据配置的事件掩码过滤事件

2. **EventLogger (事件日志记录器)**
   - 格式化事件数据为CSV格式
   - 处理重命名事件的配对逻辑
   - 管理事件队列和工作线程

3. **FileLogger (文件日志记录器)**
   - 实现日志文件写入和轮转
   - 自动压缩历史日志文件
   - 管理日志文件数量限制

4. **Config (配置管理器)**
   - 连接dconfig配置系统
   - 监听配置变更事件
   - 提供配置值缓存和访问接口

### 数据流图

```
内核模块 -> Netlink -> EventListener -> EventLogger -> FileLogger -> 日志文件
    ^                                                                      |
    |                                                                      v
dconfig配置系统 <-- Config <-- 配置变更通知 <-- 动态配置调整           日志轮转
```

## 安装和部署

### 构建依赖

- CMake 3.10+
- GCC/Clang (支持C11)
- pkg-config
- libglib2.0-dev
- libgio-2.0-dev
- libnl-3-dev
- libnl-genl-3-dev

### 编译构建

```bash
# 进入logger目录
cd src/logger

# 创建构建目录
mkdir build && cd build

# 配置构建
cmake ..

# 编译
make -j$(nproc)

# 安装 (需要root权限)
sudo make install
```

### 系统集成

1. **安装配置文件**
   ```bash
   sudo cp assets/org.deepin.anything.logger.json /usr/share/dsg/configs/org.deepin.anything/
   ```

2. **安装systemd服务**
   ```bash
   sudo cp deepin-anything-logger.service /lib/systemd/system/
   sudo systemctl daemon-reload
   ```

3. **启用并启动服务**
   ```bash
   sudo systemctl enable deepin-anything-logger
   sudo systemctl start deepin-anything-logger
   ```

## 使用方法

### 服务管理

```bash
# 查看服务状态
sudo systemctl status deepin-anything-logger

# 启动服务
sudo systemctl start deepin-anything-logger

# 停止服务
sudo systemctl stop deepin-anything-logger

# 重启服务
sudo systemctl restart deepin-anything-logger

# 查看服务日志
sudo journalctl -u deepin-anything-logger -f
```

### 日志查看

```bash
# 查看最新的事件日志
sudo tail -f /var/log/deepin/deepin-anything-logger/events.csv

# 查看历史日志文件
ls -la /var/log/deepin/deepin-anything-logger/

# 解压查看压缩的历史日志
zcat /var/log/deepin/deepin-anything-logger/events.csv.1.gz
```

### 配置调优

根据系统使用情况，可以调整以下配置：

- **高频使用环境**: 增加 `log_file_size` 和 `log_file_count`
- **存储空间受限**: 减少 `log_file_count` 和 `log_file_size`
- **调试分析**: 启用 `print_debug_log`
- **特定监控需求**: 自定义 `log_events_type`

## 故障排除

### 常见问题

1. **服务启动失败**
   - 检查是否以root权限运行
   - 确认内核模块是否已加载
   - 检查日志目录权限

2. **无法记录事件**
   - 验证 `log_events` 配置是否为true
   - 检查 `log_events_type` 配置是否正确
   - 确认内核模块与用户态程序版本匹配

3. **日志文件过大**
   - 调整 `log_file_size` 配置
   - 增加 `log_file_count` 以保留更多历史
   - 检查磁盘空间是否充足

### 调试方法

```bash
# 启用调试日志
dconf write /org/deepin/anything/logger/print_debug_log 'true'

# 查看详细日志输出
sudo journalctl -u deepin-anything-logger -f --all

# 手动运行程序进行调试
sudo /usr/bin/deepin-anything-logger
```

## 开发指南

### 代码结构

```
src/logger/
├── main.c                        # 主程序入口
├── config.c/.h                   # 配置管理模块
├── datatype.c/.h                 # 数据类型定义
├── dconfig.c/.h                  # DConfig接口模块
├── event_listener.c/.h           # 事件监听模块
├── event_logger.c/.h             # 事件日志模块
├── file_log.c/.h                 # 文件日志模块
├── log.c/.h                      # 通用日志模块
├── CMakeLists.txt                # 主构建配置
└── tests/                        # 单元测试目录
    ├── CMakeLists.txt            # 测试构建配置
    ├── run_tests.sh              # 测试运行脚本
    ├── test_event_logger.c       # 事件日志模块测试
    └── test_file_log.c           # 文件日志模块测试
```

### 单元测试

项目要求单元测试覆盖率达到 **80%以上**，当前已实现以下测试模块：

#### 已实现的测试模块

**1. 事件日志模块测试 (`test_event_logger.c`)**
- 涵盖10个测试案例，全面测试EventLogger组件
- 测试范围包括：
  * EventLogger创建和销毁
  * 启动/停止功能验证
  * 基本事件日志记录
  * CSV字段转义处理
  * 重命名事件配对逻辑
  * 未配对重命名事件处理
  * 多事件并发处理
  * 停止状态下的日志记录
  * 处理器错误恢复机制
  * 性能负载测试（1000事件处理）

**2. 文件日志模块测试 (`test_file_log.c`)**
- 涵盖6个测试案例，全面测试FileLogger组件
- 测试范围包括：
  * 文件日志器创建和初始化
  * 基本日志写入功能
  * 日志文件轮转机制
  * 空指针安全性检查
  * getter函数功能验证
  * 嵌套目录自动创建

#### 测试覆盖率状态

| 模块 | 测试状态 | 测试案例数 | 覆盖特性 |
|------|----------|------------|----------|
| event_logger.c | ✅ 完整测试 | 10个案例 | 创建/销毁、事件处理、CSV格式化、错误处理 |
| file_log.c | ✅ 完整测试 | 6个案例 | 文件操作、轮转、安全性、目录创建 |
| **已测试模块** | **2/7** | **16个案例** | **核心日志功能已覆盖** |

#### 待完善的测试模块

| 模块 | 优先级 | 建议测试内容 |
|------|--------|--------------|
| config.c | 高 | 配置读取、验证、默认值处理 |
| datatype.c | 高 | 数据类型转换、枚举映射、边界检查 |
| event_listener.c | 中 | netlink通信、事件解析、错误恢复 |
| dconfig.c | 中 | dconfig接口、动态配置、回调处理 |
| main.c | 低 | 主流程、信号处理、资源清理 |

#### 运行测试

**使用自动化脚本（推荐）：**
```bash
# 完整测试流程（构建 + 运行）
cd src/logger/tests
./run_tests.sh

# 仅构建测试
./run_tests.sh --build

# 仅运行已构建的测试
./run_tests.sh --test

# 详细输出模式
./run_tests.sh --verbose

# 生成覆盖率报告
./run_tests.sh --coverage

# 运行内存检查
./run_tests.sh --memcheck

# 运行特定测试
./run_tests.sh --specific EventLoggerTest
./run_tests.sh --specific FileLogTest

# 清理构建文件
./run_tests.sh --clean
```

**使用CMake命令：**
```bash
# 构建测试程序
mkdir build-test && cd build-test
cmake .. -DENABLE_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# 运行所有单元测试
ctest --verbose

# 运行特定测试模块
ctest --verbose -R EventLoggerTest
ctest --verbose -R FileLogTest

# 生成覆盖率报告 (需要Debug构建)
make coverage
firefox coverage_html/index.html

# 内存泄漏检测 (需要安装valgrind)
make memcheck

# 显示测试统计信息
make test_stats

# 构建所有测试
make all_tests
```

#### 测试环境要求

**系统依赖：**
- GLib 2.0 测试框架
- pkg-config
- libnl-3.0, libnl-genl-3.0
- valgrind (用于内存检查)
- lcov (用于覆盖率报告)

**运行权限：**
- 测试可在普通用户权限下运行
- 测试使用 `/tmp` 目录进行临时文件操作
- 无需root权限或特殊系统配置

### 编码规范

项目遵循deepin C/C++编码规范：
- 使用4空格缩进
- 函数名使用下划线命名法
- 结构体使用驼峰命名法
- 所有公共API必须有详细注释
- 错误处理使用GError机制

### 扩展开发

1. **添加新的事件类型**
   - 在 `datatype.c` 中添加事件类型映射
   - 更新配置模式文件
   - 修改事件处理逻辑

2. **自定义日志格式**
   - 修改 `event_logger.c` 中的格式化函数
   - 更新日志解析工具
   - 考虑向后兼容性

## 性能考虑

### 系统资源占用

- **内存使用**: 约2-5MB基础内存占用
- **CPU使用**: 事件处理期间短暂CPU占用
- **磁盘I/O**: 受日志记录频率和文件大小配置影响
- **网络**: 仅使用本地netlink通信

### 性能优化建议

1. **合理配置事件类型**: 只监听必要的事件类型
2. **适当的日志轮转**: 平衡存储空间和历史保留需求
3. **监控系统负载**: 在高I/O环境下调整配置参数

## 安全考虑

### 权限要求

- 服务必须以root权限运行
- 日志文件仅root用户可读写
- 配置修改需要相应权限

### 安全特性

- 路径信息经过适当转义
- 防止日志注入攻击
- 限制日志文件大小防止磁盘填满

## 版权和许可

Copyright (C) 2025 UOS Technology Co., Ltd.
SPDX-License-Identifier: GPL-3.0-or-later

本项目基于 GPL-3.0-or-later 许可证开源，详情请参考项目根目录的 LICENSE 文件。

## 项目状态和改进建议

### 当前实现优势

1. **架构清晰**: 模块化设计，职责分离明确
2. **配置灵活**: 支持动态配置和实时调整
3. **性能良好**: 异步事件处理，不阻塞文件系统操作
4. **日志完整**: 详细记录操作上下文信息
5. **核心测试完备**: 事件日志和文件日志模块已有完整测试覆盖

### 当前开发状态

#### 完成项 ✅
- [x] 核心日志记录功能实现
- [x] 事件监听和处理机制
- [x] 动态配置管理系统
- [x] 日志轮转和压缩功能
- [x] systemd服务集成
- [x] 核心模块单元测试（event_logger, file_log）
- [x] 自动化测试脚本
- [x] 内存检查和覆盖率报告

#### 进行中 🔄
- [ ] 完善剩余模块的单元测试
- [ ] 性能基准测试建立
- [ ] 错误恢复机制优化

#### 待开发 📋
- [ ] 配置参数验证增强
- [ ] 集成测试套件
- [ ] 性能监控指标
- [ ] 日志分析工具

### 优先改进方向

#### 高优先级
1. **完善单元测试**: 为config、datatype、event_listener、dconfig模块添加测试
   - 当前测试覆盖率：2/7模块（29%）
   - 目标覆盖率：7/7模块（100%）
   - 预期工作量：约2-3周

2. **增强错误恢复**: 改进网络连接中断的恢复机制
   - netlink连接重建逻辑
   - 事件丢失检测和告警
   - 服务自愈机制

3. **配置验证**: 加强配置参数有效性检查
   - 范围和类型验证
   - 配置冲突检测
   - 默认值回退机制

#### 中优先级
4. **性能监控**: 添加内置性能指标统计
   - 事件处理延迟统计
   - 队列深度监控
   - 资源使用率跟踪

5. **集成测试**: 建立端到端测试框架
   - 内核模块与用户态交互测试
   - 多进程并发测试
   - 长时间稳定性测试

#### 低优先级
6. **日志分析**: 提供日志分析和可视化工具
   - 事件统计报表生成
   - 热点文件分析
   - 用户行为模式识别

### 建议的功能扩展

1. **事件过滤规则**: 支持基于路径模式的高级过滤
   - 正则表达式路径匹配
   - 文件类型过滤器
   - 用户/进程白名单

2. **实时通知**: 支持向其他服务发送实时事件通知
   - D-Bus消息总线集成
   - WebSocket推送接口
   - 插件化通知机制

3. **统计报告**: 定期生成文件系统活动统计报告
   - 每日/周/月活动摘要
   - 异常行为检测报告
   - 性能趋势分析

4. **集群支持**: 支持多机器环境的事件聚合
   - 中央化日志收集
   - 分布式事件关联
   - 跨节点统计分析

### 质量目标

| 指标 | 当前状态 | 目标状态 | 达成时间 |
|------|----------|----------|----------|
| 单元测试覆盖率 | 29% (2/7模块) | 100% (7/7模块) | 4周内 |
| 代码覆盖率 | 未测量 | >80% | 6周内 |
| 内存泄漏 | 0已知问题 | 0问题 | 持续 |
| 性能基准 | 未建立 | 已建立 | 8周内 |
| 文档完整性 | 90% | 95% | 2周内 |

---

**注意**: 本服务作为系统核心组件运行，任何配置修改都应谨慎测试，避免影响系统稳定性。在生产环境部署前，建议先在测试环境验证配置和功能的正确性。 