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
检查，以及控制面健康、数据面读取真实抓包与控制面首页的端到端冒烟。核心
链路用随仓库分发的夹具完整验证。本地可用
`make build && make test` 加上述命令复现同一套检查。

## 运行演示

```bash
make web                 # 终端一

cargo run --bin sentineld -- \
  --pcap data/testdata/curl-enabled-tls13.pcap \
  --rules data/ja3_rules.tsv   # 终端二
```

`make web` 会自动打开浏览器；无图形环境用 `./bin/sentinel-web --no-open`。
上述夹具为真实良性流量，不会产生告警；命中规则的真实流量才会出现在时间线。

## 目录结构

```text
bpf/        # eBPF 程序（后续切片填充）
cpp/        # C++20 算法库（C ABI）
crates/     # Rust workspace
data/       # JA3 规则表与脱敏测试夹具
go/         # Go 控制面与嵌入式前端
docs/tmp/   # 本地临时文件（不提交 git）
stats/      # 本地统计与模拟脚本（不提交 git）
.archive/   # 旧原型归档（只读，不提交 git）
```

## 注释规范

- 非平凡函数前一行说明功能与返回值；
- 复杂算法前注明算法名与输入输出；
- unsafe 或易误解边界前加 `// 注意：`；
- 不写逐行翻译式注释；代码优先自解释。

## 提交约定

- 从 `develop` 拉功能分支，PR 合入；
- 一个 PR 只解决一个问题，不做无关重构；
- 提交信息：改动少时一句话概括，改动多时分点阐述；不写营销式或 AI 风格
  描述。

## 规则数据

- `data/ja3_rules.tsv`：制表符分隔 `指纹 名称 严重度 来源`，来源列必须注明
  权威出处；禁止编造指纹与占位条目；
- `data/testdata/`：脱敏公开抓包夹具（Apache-2.0 来源，附许可声明），核心
  测试的默认依赖；
- 合成流量仅允许放在 `stats/sim/`（不提交 git），不得进入测试、规则表或
  产品路径。

## 工程标准

- 禁止模拟数据：测试夹具、检测规则、基准与文档一律使用真实抓包与权威情报；
  切片期间若存在临时演示夹具，必须在代码中标注并在阶段收口前删除。
- 禁止硬编码：端口、地址、路径、密钥与规则表必须来自配置文件或权威数据源；
  编译期常量只允许用于不可变的算法参数（如 MD5 常量），不得承载运行时配置。
- 端口策略（Fail-fast）：默认端口 21318，被占用时立即报致命错误并以非零
  状态码退出；禁止向外部进程发送 SIGTERM/SIGKILL，禁止自动绑定下一可用
  端口。端口变更只能通过 `--addr` 或配置文件显式指定。
- 测试可复现：核心测试夹具随仓库分发，`make test` 完整覆盖核心链路；用例
  缺少必要夹具必须判失败，禁止静默跳过。依赖私有抓包的扩展测试仅通过功能
  标志或专门 Make 目标触发。
