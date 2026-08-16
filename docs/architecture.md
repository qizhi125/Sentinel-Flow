# 架构说明

## 目标与非目标

首个可用版本的目标：

- 面向个人与边缘节点的实时安全事件时间线；
- 通过 JA3 指纹与经典规则检测已知恶意 TLS 通信；
- 在 4 核 / 8 GB 机器上以约 50 Mbps 负载稳定运行。

1.0 明确不做：Windows 支持、流量解密、数据面重型模型推理、企业 SOC 能力。

## 分层与模块

| 层 | 语言 | 位置 | 职责 |
|----|------|------|------|
| eBPF（规划中） | Rust（aya-ebpf） | `bpf/` | XDP 过滤与 XskMap 重定向 |
| 数据面 | Rust | `crates/capture`、`crates/detect`、`crates/core` | 抓包、检测调度、进程生命周期 |
| 算法库 | C++20 | `cpp/` | JA3 计算；后续 Aho-Corasick、PCRE2、Hyperscan |
| 规则数据 | TSV | `data/` | JA3 指纹与情报来源，加载后进入检测器 |
| FFI | Rust | `crates/ffi` | 编译 C++ 静态库并做安全封装 |
| 告警传输 | Rust → Go | `crates/api` | HTTP+JSON 上报 `/v1/ingest` |
| 控制面 | Go | `go/` | ingest、环形缓冲、SSE、嵌入式前端 |
| 前端 | JS/HTML/CSS | `go/web/` | EventSource 消费事件流 |

## 当前数据流（v0.1.0 垂直切片）

```text
离线 pcap → TCP 载荷 → TLS ClientHello → C++ JA3 → 指纹匹配
  → POST /v1/ingest → Go 环形缓冲（1000）→ SSE /v1/events → 浏览器时间线
```

当前切片为单线程顺序处理；Go 侧用一个互斥锁保护环形缓冲，订阅者通过缓冲
通道接收事件，慢订阅者丢弃事件而不阻塞 ingest。

## 语言边界

- C++ 只暴露 `extern "C"` 函数，不持有 socket、线程或内存；缓冲由调用方
  分配，C++ 写入前做边界检查。
- Rust 拥有 socket、ring、线程与 eBPF 加载；unsafe 仅存在于 `crates/ffi`。
- Go 只处理 HTTP，不直接调用 C++ 或 libpcap。
- Rust 与 Go 之间只传告警/事件/统计（HTTP+JSON），不传每包数据。

## 安全与性能边界

- 控制面 `/v1/ingest` 与 `/v1/events` 默认仅绑定回环地址（已实现）；
- 规划中：对外暴露要求配置鉴权或 mTLS；处理延迟超阈值或环形缓冲满载时触发
  背压，数据面丢弃低优先级报文并记录速率限制审计日志；
- 跨语言边界（FFI 与 HTTP+JSON）的性能成本需在 50 Mbps 目标负载下实测，
  作为 0.2 eBPF 路径的对比基线。

## 未来 eBPF 数据面

- aya-rs 加载并附加 XDP 程序，管理 XskMap；
- XDP 过滤 IPv4 并把流量重定向到各队列 AF_XDP socket；
- 用户态管理 UMEM 与 Fill/Completion/Rx/Tx 四环，把帧交给与 pcap 路径相同
  的 `Detector` 注册表；
- 要求内核 >= 5.15 且开启 BTF/CO-RE，容器部署需要
  `--net=host --cap-add NET_RAW,NET_ADMIN,BPF`。

## 决策摘要

- 基线抓包用 pcap，eBPF 作为可选快路径，不进入 1.0 必选能力；
- 版本基点为 0.1.0：迭代升补丁位，重大功能升中间位，1.0 后的大型更新才升
  主版本；
- 旧原型位于 `.archive/`，只读参考，不参与构建。
