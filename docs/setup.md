# 环境搭建与构建指南

## 1. 环境依赖

- **操作系统**：Linux（推荐 Fedora 42/43、Ubuntu 24.04+）
- **编译器**：GCC 14+ 或 Clang 18+（支持 C++20）
- **构建工具**：CMake 3.20+、Ninja（推荐）
- **Go**：1.25.x
- **Clang**：用于 eBPF 交叉编译至 BPF 目标

### 依赖库

| 包名 | 用途 | 必需 |
|------|------|------|
| `libpcap-devel` | 数据包捕获（主要后端） | ✅ |
| `sqlite-devel` | 告警/规则持久化（WAL 模式） | ✅ |
| `libbpf-devel` | eBPF/XDP 后端 | 可选 (`BUILD_EBPF=ON`) |
| `libxdp-devel` | XDP 用户态工具 | 可选 |

### 安装依赖（Fedora）

```bash
sudo dnf update -y
sudo dnf install -y cmake gcc-c++ libpcap-devel sqlite-devel libbpf-devel clang golang
```

### 安装依赖（Ubuntu）

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ clang libpcap-dev libsqlite3-dev libbpf-dev libxdp-dev golang
```

## 2. 构建 C++ 核心库

```bash
cd Sentinel-Flow

# 构建 release 版本
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 构建 debug 版本（含 sanitizer）
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DBUILD_EBPF=ON
cmake --build build -j$(nproc)
```

编译成功后将生成静态库 `build/libsentinel/libsentinel_core.a`。

## 3. 构建 Go CLI 工具

```bash
go build -o sentinel-cli ./cmd/sentinel
```

Go 模块依赖项：
- `gopkg.in/yaml.v3` — YAML 规则文件解析
- `github.com/fsnotify/fsnotify` — 规则文件热重载监听

## 4. 运行测试

### C++ 单元测试

```bash
# 构建测试目标
cmake --build build --target sentinel_tests -j$(nproc)

# 通过 CTest 运行
cd build && ctest --output-on-failure

# 或直接运行
./build/tests/sentinel_tests
```

### Go 测试

```bash
# 需要先构建 C++ 库
go test ./pkg/engine/ -v
```

## 5. 代码格式化与检查

```bash
# C++ 格式化
find libsentinel tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i -style=file

# C++ 格式检查（CI 使用 --dry-run --Werror）
find libsentinel tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror -style=file

# Go 格式化
gofmt -w cmd/sentinel/ pkg/engine/
```

## 6. 赋予网络权限并运行

```bash
# 授予抓包权限（无需 sudo）
sudo setcap cap_net_raw,cap_net_admin+eip ./sentinel-cli

# 验证权限已附加
getcap ./sentinel-cli

# 启动引擎（默认捕获 lo 接口）
./sentinel-cli
```

若需使用 eBPF 模式，请确保已加载 XDP 程序并指定 `--ebpf` 参数。

## 7. 命令行参数

```bash
sentinel-cli -i eth0 -r ./configs/rules.yaml -w 4 --ebpf
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-i` | `lo` | 网络接口名称（如 eth0、lo） |
| `-r` | `./configs/rules.yaml` | YAML 规则文件路径 |
| `-w` | `4` | 工作线程数（1-64） |
| `--ebpf` | `false` | 启用 AF_XDP 零拷贝捕获 |
| `--offline` | `""` | 离线 PCAP 文件路径 |
| `-v` | `false` | 启用详细日志 |
| `-h` | `false` | 显示帮助 |

## 8. IDE 配置

### CLion

- 打开项目根目录，CLion 会自动检测 CMakeLists.txt
- 编译数据库：`compile_commands.json` 由 CMake 在构建时生成
- clangd 配置：参见 `.clangd` 文件

### GoLand

- Go 模块路径：`github.com/qizhi125/Sentinel-Flow`
- GOROOT：自动检测 `/usr/lib/golang`

## 9. CI/CD

CI 流水线配置位于 `.github/workflows/ci.yml`，涵盖以下任务：
- **build-and-test**：Ubuntu 上的 Release/Debug 矩阵 + sanitizer 变体
- **asan-ubsan**：Debug 构建 + AddressSanitizer + UndefinedBehaviorSanitizer
- C++ 单元测试通过 `ctest --output-on-failure` 执行
- Go 构建和测试在非 sanitizer 作业中运行
- C++ 格式检查使用 `clang-format --dry-run --Werror`
- Go 代码检查使用 `golangci-lint`

## 10. 常见问题

### setcap 命令不存在

```bash
# Debian/Ubuntu
sudo apt install libcap2-bin

# Fedora
sudo dnf install libcap
```

### 文件系统不支持扩展属性

某些文件系统（如 tmpfs、NFS）可能不支持 `setcap`。请将二进制文件移动至支持的文件系统（如 ext4、xfs）再执行命令。

### 重新编译后需重新执行 setcap

若二进制文件被更新（重新编译），需重新执行 `setcap`。
