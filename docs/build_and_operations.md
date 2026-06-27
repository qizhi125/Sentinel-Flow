> **文档状态**: Active
> **最后更新**: 2026-06
> **所属子系统**: Global

# 构建与运维指南

## 1. 环境依赖

| 包名 | 版本要求 | 用途 |
|------|---------|------|
| `cmake` | >= 3.20 | C++ 构建系统 |
| `gcc-c++` 或 `clang` | >= 14 / >= 18 | C++20 编译 |
| `libpcap-devel` | — | 数据包捕获 |
| `sqlite-devel` | — | 告警持久化（WAL 模式） |
| `golang` | >= 1.25 | Go CLI 编译 |

### Fedora

```bash
sudo dnf install -y cmake gcc-c++ libpcap-devel sqlite-devel golang
```

### Ubuntu

```bash
sudo apt-get install -y cmake g++ libpcap-dev libsqlite3-dev golang
```

## 2. 构建

### C++ 静态库

```bash
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sentinel_core -j$(nproc)
# 产物: build/lib/libsentinel_core.a

# Debug + Sanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build --target sentinel_core -j$(nproc)
```

### Go CLI

```bash
go mod tidy
go build -o sentinel-cli ./cmd/sentinel
```

## 3. 权限配置

抓包需要原始套接字与混杂模式权限。

### 方案 A：setcap（推荐，无需 root 运行）

```bash
sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli
getcap ./sentinel-cli
# → ./sentinel-cli cap_net_raw,cap_net_admin=eip
```

| 能力 | 作用 |
|------|------|
| `cap_net_raw` | 原始套接字（`AF_PACKET`） |
| `cap_net_admin` | 混杂模式、BPF 过滤器 |
| `=eip` | effective + inheritable + permitted |

### 方案 B：sudo

```bash
sudo ./sentinel-cli -i eth0 -c configs/rules.yaml
```

### 故障排查

| 问题 | 检查 |
|------|------|
| 引擎启动失败 | `getcap ./sentinel-cli` 确认权限生效 |
| 文件系统不支持 | `tmpfs`/`NFS` 不支持 `setcap`，移至 `ext4`/`xfs` |
| 重新编译后失效 | 新 inode 需重新执行 `setcap` |
| SELinux 拦截 | 临时 `sudo setenforce 0` 或配置策略 |

## 4. 运行

```bash
./sentinel-cli -i lo -c configs/rules.yaml
```

CLI 参数：

```
sentinel-cli [flags]
  -c  string  YAML 规则配置文件路径 (默认: configs/rules.yaml)
  -i  string  网络接口名称 (默认: lo)
```

## 5. 性能调优

### 瓶颈识别

| 症状 | 原因 | 检查命令 |
|------|------|----------|
| 网卡丢包 `rx_dropped` 增加 | 内核缓冲区不足 | `ethtool -S eth0 \| grep drop` |
| 告警队列积压 | 磁盘 I/O 瓶颈 | `iostat -x 1` |
| Dashboard PPS 停滞 | 规则过多或载荷过大 | 检查规则数量与 payload 长度 |

### 系统级参数

```bash
# 增大网卡环形缓冲区
ethtool -g eth0                          # 查看当前值
ethtool -G eth0 rx 4096 tx 4096         # 设为最大值

# 内核网络参数
sysctl -w net.core.rmem_max=26214400
sysctl -w net.core.rmem_default=26214400

# RPS 分散软中断负载
echo 7 > /sys/class/net/eth0/queues/rx-0/rps_cpus
```

### SQLite 参数

```sql
PRAGMA cache_size = 10000;       -- 增大页面缓存
PRAGMA synchronous = NORMAL;     -- 降低同步级别（已默认）
PRAGMA journal_mode = WAL;       -- WAL 模式（已默认）
VACUUM;                          -- 定期回收空间
```

数据库路径由 `DatabaseManager` 构造参数指定。

## 6. 典型部署场景

### 开发调试

```
硬件: 4 核, 8GB, SSD
预期: 100 Mbps 以下
命令: ./sentinel-cli -i lo -c configs/rules.yaml
      （回环接口，无需 setcap，使用测试规则集）
```

### 生产旁路监听

```
硬件: 8 核, 16GB, SSD
预期: 1 Gbps, 50k pps
配置:
  ./sentinel-cli -i eth0 -c configs/production.yaml
  ethtool -G eth0 rx 4096
  SQLite: cache_size=10000
  SPSCQueue 容量: 编译期调整模板参数
```

## 7. 监控诊断

### 内部指标（Dashboard 实时渲染）

| 指标 | 获取方式 |
|------|---------|
| 吞吐量（pps） | Dashboard 核心状态面板 |
| 队列深度 / 缓冲区使用率 | Dashboard KPI 卡片 |
| 告警总数 | Dashboard 核心面板 + 关机总结 |
| 协议分布（TCP/UDP/HTTP） | Dashboard 流量面板 |
| 峰值 pps | Dashboard KPI 卡片 |

### 外部工具

```bash
perf top -p $(pgrep sentinel-cli)    # 热点函数
iostat -x 1                           # 磁盘 I/O
mpstat -P ALL 1                       # 各核心负载
```

## 8. 规则热重载

规则文件（`-c` 参数指定）由 `fsnotify` 实时监控。检测到文件变更时：

1. 500ms 防抖（避免编辑器连续写入触发多次重建）
2. `clear_rules` → `add_rule × N` → `build_matcher`（串行执行，持有 `reloadMu`）
3. 重建完成后新规则立即生效，正在进行的匹配使用旧自动机直至下一次 `match()` 调用

**约束**：热重载期间匹配操作不受影响（旧自动机仍在内存中），但新的 `add_rule` / `build_matcher` 调用被 `reloadMu` 阻塞。

## 9. 关机流程

```
SIGINT / SIGTERM
  → Dashboard.Quit()      (TUI 退出)
  → Engine.Stop()          (PcapCapture::stop() + Pipeline::stop())
  → 关机总结 (输出到 stderr)
  → os.Exit(0)
```

## 10. 测试

```bash
# C++ 单元测试
cmake --build build --target sentinel_tests -j$(nproc)
cd build && ctest --output-on-failure

# Go 测试
go test ./pkg/engine/ -v
```

## 11. 代码格式化

```bash
# C++
find libsentinel tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i -style=file

# Go
gofmt -w cmd/ pkg/
```

## 12. IDE 配置

- **CLion**: 打开项目根目录，自动检测 `CMakeLists.txt`。`compile_commands.json` 由 CMake 生成（已默认启用 `CMAKE_EXPORT_COMPILE_COMMANDS=ON`）
- **GoLand / VS Code**: Go 模块路径 `github.com/qizhi125/Sentinel-Flow`

## 13. 常见问题

### `setcap` 不可用

```bash
# Debian/Ubuntu
sudo apt install libcap2-bin
# Fedora
sudo dnf install libcap
```

### SELinux/AppArmor 拦截

```bash
# 临时测试
sudo setenforce 0
# 或配置策略允许 cap_net_raw
```
