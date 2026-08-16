# 开发与构建

## 依赖

- Linux、g++（GCC 12+）、ar
- Rust 1.75+
- Go 1.25+

当前切片不依赖外部 crate 或 Go 模块；C++ 库由 `crates/ffi/build.rs` 编译。

## 构建与测试

```bash
make build
make test
```

`make build` 依次执行 `cargo build --workspace` 与 `go build`。C++ 以
`-Wall -Wextra -Werror` 编译，任何告警或错误都会让整体构建失败。

提交前检查：

```bash
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
gofmt -l go
go vet ./go/...
```

## 持续集成

`.github/workflows/ci.yml` 在 push 到 `main`/`develop` 与 PR 时运行：构建
（含 `build.rs` 驱动的 C++ 编译）、Rust/Go 测试、clippy 与 go vet、格式
检查，以及“生成 pcap → sentineld → SSE 告警”端到端冒烟。本地可用
`make build && make test` 加上述命令复现同一套检查。

## 运行演示

```bash
make web                                                    # 终端一
cargo run --example gen_demo -- /tmp/sentinel-demo.pcap      # 终端二
cargo run --bin sentineld -- --pcap /tmp/sentinel-demo.pcap
```

浏览器打开 `http://localhost:21318`。

## 目录结构

```text
bpf/        # eBPF 程序（后续切片填充）
cpp/        # C++20 算法库（C ABI）
crates/     # Rust workspace
go/         # Go 控制面与嵌入式前端
docs/tmp/   # 本地临时文件（不提交 git）
.archive/   # 旧原型归档（只读，不提交 git）
```

## 注释规范

- 非平凡函数前一行说明功能与返回值；
- 复杂算法前注明算法名与输入输出；
- unsafe 或易误解边界前加 `// 注意：`；
- 不写逐行翻译式注释；代码优先自解释。

完整规则见根目录 [AGENT.md](../AGENT.md)。

## 提交约定

- 从 `develop` 拉功能分支，PR 合入；
- 一个 PR 只解决一个问题，不做无关重构；
- 指纹与规则条目必须区分“权威来源”与“占位”。

## 工程标准

- 禁止模拟数据：测试夹具、检测规则、基准与文档一律使用真实抓包与权威情报；
  切片期间若存在临时演示夹具，必须在代码中标注并在阶段收口前删除。
- 禁止硬编码：端口、地址、路径、密钥与规则表必须来自配置文件或权威数据源；
  编译期常量只允许用于不可变的算法参数（如 MD5 常量），不得承载运行时配置。
- 进程与端口纪律：优先使用默认推荐端口（21318），禁止强制杀掉无关进程。
  默认端口被占用时先识别占用者：是本工具遗留的旧实例则先 SIGTERM、超时后才
  SIGKILL，再复用端口；是其他活跃进程则改用下一个可用端口，并把实际端口写入
  数据面可发现的固定位置。
  注意：操作系统层面的僵尸进程不持有监听 socket；“遗留实例”指上次运行未
  正常退出的活跃进程。
