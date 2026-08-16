# Sentinel-Flow

面向个人与边缘节点的 Linux NIDS，长期目标是 aya-rs + XDP/AF_XDP 数据面。
当前仓库包含第一条垂直切片：离线 pcap → TLS ClientHello → C++ 计算 JA3 →
指纹匹配 → HTTP 告警 → SSE 网页时间线。

## 目录结构

```text
bpf/        # eBPF 程序目录（后续切片填充）
cpp/        # C++20 算法库，仅暴露 C ABI（当前实现 JA3）
crates/     # Rust workspace：capture / detect / ffi / api / core
go/         # Go 控制面与嵌入式前端
```

## 依赖

- Linux、g++（GCC 12+）、ar
- Rust 1.75+
- Go 1.25+

当前切片不依赖任何外部 crate 或 Go 模块；C++ 库由 `crates/ffi/build.rs` 编译。

## 构建与测试

```bash
make build
make test
```

## 运行演示

终端一：

```bash
make web
```

终端二：

```bash
cargo run --example gen_demo -- /tmp/sentinel-demo.pcap
cargo run --bin sentineld -- --pcap /tmp/sentinel-demo.pcap
```

浏览器打开 `http://localhost:21318`，约一秒内即可看到告警时间线。

控制面默认监听 `21318`（`0x5346`，取自 “SF”），可用
`go run ./go/cmd/sentinel-web --addr :端口` 覆盖；数据面用
`--api http://localhost:端口` 指定上报地址。

## 当前限制

- 仅支持经典 pcap，不接受 pcapng；
- ClientHello 必须位于单个 TCP 段内，尚未做 TCP 重组；
- 指纹表包含四条公开文档化指纹与六条明确标注的占位条目，发布前需替换为
  权威情报源；
- 实时抓包、eBPF、TLS 元数据与熵统计尚未实现。

## 文档

- [架构说明](docs/architecture.md)
- [开发与构建](docs/development.md)
- [路线图](docs/roadmap.md)

`docs/tmp/` 用于存放本地临时文件，已加入 `.gitignore`，不会提交到 git。

## 许可证

[MIT](LICENSE)
