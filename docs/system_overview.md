<h1 align="center">Sentinel-Flow NIDS</h1>

<p align="center"><em>C++20 无锁管线 | Go Bubbletea TUI | SQLite WAL 持久化</em></p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=c%2B%2B" alt="C++20"/>
  <img src="https://img.shields.io/badge/Go-1.25-00ADD8?logo=go" alt="Go 1.25"/>
  <img src="https://img.shields.io/badge/SQLite-WAL-003B57?logo=sqlite" alt="SQLite WAL"/>
  <img src="https://img.shields.io/badge/License-MIT-yellow" alt="MIT License"/>
</p>

---

> **文档状态**: Active &nbsp;|&nbsp; **最后更新**: 2026-06 &nbsp;|&nbsp; **所属子系统**: Global

# 系统概览

Sentinel-Flow 是面向 Linux 的网络入侵检测系统（NIDS）。C++20 数据面负责实时抓包与多模式匹配，Go 控制面通过 CGO 驱动引擎并渲染终端仪表盘，告警通过 SQLite WAL 异步批量落盘。

## 核心特性

- **Aho-Corasick 匹配**：O(N) 单遍扫描，全跃迁表无回溯
- **无锁数据面**：SPSC 环形队列（`acquire`/`release` 内存序）+ Tagged Pointer 对象池（64-bit tag 防 ABA）
- **终端仪表盘**：Bubbletea 组件化 TUI，Lipgloss ANSI 渲染，事件驱动差异刷新
- **规则热重载**：`fsnotify` 监听 YAML 变更，500ms 防抖后原子重建 AC 自动机
- **异步持久化**：`DatabaseManager` 双缓冲批量落盘，SQLite WAL 模式，不阻塞检测管线

## 架构

```
捕获驱动 (libpcap/PcapCapture)
  → PacketParser (L3/L4 协议解析)
    → Pipeline (单线程 pop → parse → match)
      → AhoCorasick::match() (O(N) 多模式匹配)
        → CGO 回调 → Go Dashboard (Bubbletea/Lipgloss)
          → DatabaseManager (SQLite WAL, 双缓冲异步落盘)
```

| 层级 | 语言 | 组件 | 职责 |
|------|------|------|------|
| 数据面 | C++20 | PcapCapture, PacketParser, AhoCorasick, Pipeline | 抓包 → 解析 → 匹配 |
| 存储层 | C++20 | DatabaseManager | SQLite WAL 批量告警落盘 |
| 控制面 | Go | engine.Engine, config.Watcher | CGO 绑定 + fsnotify 热重载 |
| UI 层 | Go | ui.Dashboard | Bubbletea/Lipgloss 终端仪表盘 |

## 快速开始

### 依赖

- Linux（Fedora 42+ / Ubuntu 24.04+）
- GCC 14+ 或 Clang 18+（C++20）、Go 1.25+
- CMake 3.20+, libpcap-devel, sqlite-devel

```bash
# Fedora
sudo dnf install -y cmake gcc-c++ libpcap-devel sqlite-devel golang

# Ubuntu
sudo apt-get install -y cmake g++ libpcap-dev libsqlite3-dev golang
```

### 构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sentinel_core -j$(nproc)
# 产物: build/lib/libsentinel_core.a

go mod tidy
go build -o sentinel-cli ./cmd/sentinel

sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli
./sentinel-cli -i lo -c configs/rules.yaml
```

### CLI 参数

```
sentinel-cli [flags]
  -c  string  YAML 规则配置文件路径 (默认: configs/rules.yaml)
  -i  string  网络接口名称 (默认: lo)
```

## 终端约束

TUI 仪表盘基于 ANSI 终端渲染，存在以下硬性约束：

| 约束 | 阈值 | 说明 |
|------|------|------|
| 终端列宽 | >= 100 列 | 低于 100 列时布局缩放到 100 列，左侧内容被截断 |
| 终端行高 | >= 26 行 | 低于 26 行时视口高度压缩至最低 4 行 |
| 等宽字体 | 必须 | 非等宽字体导致表格列错位与 Unicode 字符宽度误判 |
| Unicode / Emoji | 终端需支持 | 仪表盘使用 Unicode 符号表示威胁等级与状态 |

### 已知问题

- **ANSI 转义序列污染**：原始网络载荷中与终端控制码冲突的字节序列导致布局撕裂。输入净化管线 `sanitize()`（正则 `[^[:print:]]` → `.`）+ `runewidth.Truncate` 解决
- **UTF-8 多字节边界**：以字节偏移切片多字节字符产生替换字符 U+FFFD。所有截断操作使用 `runewidth.RuneWidth()` 遍历 rune 边界

## 规则配置

`configs/rules.yaml`:

```yaml
rules:
  - id: 1001
    enabled: true
    protocol: "ANY"
    pattern: "attack_pattern"
    level: 4
    description: "检测到攻击载荷"
```

Go 侧通过 `fsnotify` 监听文件变更，500ms 防抖后自动热重载 AC 自动机。

## 测试

```bash
# C++ 单元测试
cmake --build build --target sentinel_tests -j$(nproc)
cd build && ctest --output-on-failure

# Go 测试
go test ./pkg/engine/ -v
```

## 目录结构

```
├── cmd/sentinel/              # Go CLI 入口 (main.go)
├── pkg/
│   ├── config/                # 规则加载 (rules.go) + fsnotify 热重载 (watcher.go)
│   ├── engine/                # CGO 绑定 (binding.go) + 引擎封装 (engine.go)
│   └── ui/                    # Bubbletea/Lipgloss 终端仪表盘 (dashboard.go, controller.go)
├── libsentinel/               # C++20 核心库
│   ├── include/sentinel/
│   │   ├── capi.h             # 对外 C API
│   │   ├── capture/           # ICapture.h, PcapCapture.h, AfXdpCapture.h
│   │   ├── common/            # SPSCQueue.h, memory/ObjectPool.h
│   │   ├── engine/            # Pipeline.h, DatabaseManager.h
│   │   │   ├── flow/          # PacketParser.h
│   │   │   └── match/         # AhoCorasick.h
│   │   └── types/             # PacketTypes.h
│   └── src/                   # C++ 实现文件
├── bpf/                       # eBPF XDP 程序（AF_XDP 捕获路径）
├── docs/                       # 架构 + 边界规约 + 构建/运维指南
├── tests/                      # C++ 单元测试 (GTest)
├── configs/rules.yaml          # 示例规则配置
└── CMakeLists.txt              # 顶层 CMake (C++20 标准)
```

## 文档导航

| 文档 | 内容 |
|------|------|
| [`docs/architecture.md`](docs/architecture.md) | 架构规格 — 数据面管线、线程模型、C API 清单、数据结构 |
| [`docs/cgo_boundary.md`](docs/cgo_boundary.md) | C/Go 边界规约 — API 接口、内存所有权、CGO 回调时序 |
| [`docs/lockfree_model.md`](docs/lockfree_model.md) | 无锁内存模型 — SPSCQueue 内存序分析、ObjectPool ABA 推演 |
| [`docs/build_and_operations.md`](docs/build_and_operations.md) | 构建与运维 — 依赖安装、构建、性能调优、部署场景 |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | 贡献指南 — 分支策略、PR 流程、代码规范 |
| [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) | 行为准则 — 贡献者公约 2.1 版 |

## 许可证

[MIT License](LICENSE)
