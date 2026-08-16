# Sentinel-Flow

Sentinel-Flow 是面向个人与边缘节点的 Linux 网络入侵检测系统（NIDS）。
数据面由 Rust 实现，算法库由 C++20 通过 C ABI 提供，控制面由 Go 提供
HTTP/SSE 服务与嵌入式网页。当前版本（0.1.0）支持离线 pcap 分析与基于
JA3 指纹的 TLS 客户端检测。

## 功能

- 读取经典 pcap（支持大小端、微秒/纳秒时间戳），解析 IPv4/TCP 五元组；
- 提取 TLS ClientHello 并计算 JA3 指纹（C++20 实现，无外部依赖）；
- 按 `data/ja3_rules.tsv` 规则表匹配已知恶意客户端指纹并生成告警；
- Go 控制面通过 `/v1/ingest` 接收告警，环形缓冲保存最近 1000 条，
  `/v1/events` 以 SSE 推送，内置网页实时展示告警时间线。

## 架构

| 层 | 语言 | 位置 | 职责 |
|----|------|------|------|
| 数据面 | Rust | `crates/capture`、`crates/detect`、`crates/core` | 抓包读取、检测调度、进程生命周期 |
| 算法库 | C++20 | `cpp/` | JA3 计算，仅暴露 C ABI |
| FFI | Rust | `crates/ffi` | 编译 C++ 静态库并做安全封装 |
| 告警传输 | Rust → Go | `crates/api` | HTTP+JSON 上报 `/v1/ingest` |
| 控制面 | Go | `go/` | ingest、环形缓冲、SSE、嵌入式前端 |

数据流：

```text
离线 pcap → TCP 载荷 → TLS ClientHello → C++ JA3 → 指纹匹配
  → POST /v1/ingest → Go 环形缓冲（1000）→ SSE /v1/events → 浏览器时间线
```

## 快速开始

依赖：Linux、g++（GCC 12+）、ar、Rust 1.75+、Go 1.25+。

```bash
git clone https://github.com/qizhi125/Sentinel-Flow.git
cd Sentinel-Flow
make build
make web
```

`make web` 启动控制面并自动打开告警时间线；无图形环境时改用
`./bin/sentinel-web --no-open`。

另开终端分析真实抓包：

```bash
cargo run --bin sentineld -- --pcap <文件.pcap> --rules data/ja3_rules.tsv
```

本地运行测试（可选）：

```bash
make test
```

## 检测规则

规则位于 `data/ja3_rules.tsv`，制表符分隔：

```text
指纹<TAB>名称<TAB>严重度<TAB>来源
```

规则条目必须来自权威情报源，并在“来源”列注明出处与日期；禁止编造指纹或
使用占位条目。默认加载路径可用 `--rules` 覆盖。

## 端口

控制面默认端口 21318，可用 `--addr` 显式覆盖。端口被占用时进程立即报错退出，
不自动更换端口、不终止占用进程。数据面上报地址用 `--api` 指定。

## API 安全

`/v1/ingest` 与 `/v1/events` 默认仅绑定回环地址。规划中：对外暴露必须配置
鉴权或 mTLS；处理延迟超阈值或环形缓冲满载时触发背压，丢弃低优先级报文并
记录速率限制审计日志。

## 测试

仓库包含脱敏公开夹具，`make test` 完整覆盖核心链路；用例缺少必要夹具时判为
失败，不做静默跳过。依赖私有抓包的扩展测试通过专门 Make 目标触发。

## 当前限制

- 仅支持经典 pcap，不支持 pcapng；仅解析 IPv4/TCP；
- ClientHello 需位于单个 TCP 段内，尚未实现 TCP 重组；
- 实时抓包与 eBPF 快路径尚未实现，见[路线图](docs/roadmap.md)。

## 文档

- [架构说明](docs/architecture.md)
- [开发与构建](docs/development.md)
- [路线图](docs/roadmap.md)

## 许可证

[MIT](LICENSE)
