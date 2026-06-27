<h1 align="center">Sentinel-Flow NIDS</h1>

<p align="center"><em>C++20 无锁管线 | Go Bubbletea TUI | SQLite WAL 持久化</em></p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=c%2B%2B" alt="C++20"/>
  <img src="https://img.shields.io/badge/Go-1.25-00ADD8?logo=go" alt="Go 1.25"/>
  <img src="https://img.shields.io/badge/SQLite-WAL-003B57?logo=sqlite" alt="SQLite WAL"/>
  <img src="https://img.shields.io/badge/License-MIT-yellow" alt="MIT License"/>
</p>

---

面向 Linux 的网络入侵检测系统（NIDS）。C++20 数据面负责实时抓包与 Aho-Corasick 多模式匹配，Go 控制面通过 CGO 驱动引擎并渲染终端仪表盘，告警通过 SQLite WAL 异步批量落盘。

## 快速开始

```bash
# 构建 C++ 静态库
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sentinel_core -j$(nproc)

# 构建 Go CLI
go mod tidy
go build -o sentinel-cli ./cmd/sentinel

# 运行
sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli
./sentinel-cli -i lo -c configs/rules.yaml
```

## 文档

完整技术文档见 **[docs/system_overview.md](docs/system_overview.md)**，包括：

| 文档 | 内容 |
|------|------|
| [`docs/system_overview.md`](docs/system_overview.md) | 系统概览 — 架构、快速开始、目录结构、终端约束 |
| [`docs/architecture.md`](docs/architecture.md) | 架构规格 — 数据面管线、线程模型、C API 清单 |
| [`docs/cgo_boundary.md`](docs/cgo_boundary.md) | C/Go 边界规约 — API 接口、内存所有权、回调时序 |
| [`docs/lockfree_model.md`](docs/lockfree_model.md) | 无锁内存模型 — SPSCQueue 内存序、ObjectPool ABA 推演 |
| [`docs/build_and_operations.md`](docs/build_and_operations.md) | 构建与运维 — 依赖安装、性能调优、部署场景 |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | 贡献指南 — 分支策略、PR 流程、代码规范 |

## 许可证

[MIT License](LICENSE)
