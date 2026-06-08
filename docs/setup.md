# 环境搭建与构建指南

## 1. 依赖清单

| 包名 | 用途 | 必需 |
|------|------|------|
| `cmake` >= 3.20 | C++ 构建系统 | ✅ |
| `gcc-c++` >= 14 或 `clang` >= 18 | C++20 编译 + eBPF 交叉编译 | ✅ |
| `libpcap-devel` | 数据包捕获（主要后端 + PCAP 文件写入） | ✅ |
| `sqlite-devel` | 告警/规则持久化（WAL 模式） | ✅ |
| `golang` >= 1.25 | Go CLI 编译 | ✅ |
| `libbpf-devel` | eBPF/XDP 后端 | 可选 (`BUILD_EBPF=ON`) |
| `libxdp-devel` | AF_XDP 用户态工具 | 可选 |
| `clang` | 编译 eBPF 探针至 BPF 目标 | 可选 (`BUILD_EBPF=ON`) |

### Fedora 安装

```bash
sudo dnf install -y cmake gcc-c++ libpcap-devel sqlite-devel libbpf-devel clang golang
```

### Ubuntu 安装

```bash
sudo apt-get install -y cmake g++ clang libpcap-dev libsqlite3-dev libbpf-dev libxdp-dev golang
```

## 2. 构建

### C++ 静态库

```bash
cd Sentinel-Flow

# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# 产物: build/libsentinel/libsentinel_core.a

# Debug + Sanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DBUILD_EBPF=ON
cmake --build build -j$(nproc)
```

`BUILD_EBPF=ON`（默认）时，CMake 自动调用 `clang -target bpf` 编译 `xdp_prog.c` → `xdp_prog.o`。
需要 Linux 内核头文件 `asm/types.h`，缺失时 CMake 报 FATAL_ERROR。

### Go CLI

```bash
go build -o sentinel-cli ./cmd/sentinel
```

Go 模块依赖（`go.mod`）：`gopkg.in/yaml.v3`、`github.com/fsnotify/fsnotify`。

## 3. 运行权限

```bash
# 授予网络能力（无需 root）
sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli

# 验证
getcap ./sentinel-cli
# → ./sentinel-cli cap_net_raw,cap_net_admin=eip

# 运行
./sentinel-cli -i eth0 -r ./configs/rules.yaml
```

eBPF 模式额外需要 `CAP_BPF`（Linux >= 5.8 自动包含在 `CAP_SYS_ADMIN` 中，或使用 `cap_bpf=eip`）。

若未授予能力，引擎回退到离线模式：`./sentinel-cli --offline capture.pcap`

## 4. 测试

```bash
# C++ 单元测试
cmake --build build --target sentinel_tests -j$(nproc)
cd build && ctest --output-on-failure

# Go 测试（需先构建 C++ 库）
go test ./pkg/engine/ -v
```

## 5. 代码格式化

```bash
# C++ 格式化
find libsentinel tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i -style=file

# C++ 格式检查（CI 用）
find libsentinel tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror -style=file

# Go 格式化
gofmt -w cmd/sentinel/ pkg/engine/
```

## 6. IDE 配置

- **CLion**: 打开项目根目录，自动检测 `CMakeLists.txt`。编译数据库 `compile_commands.json` 由 CMake 生成
- **GoLand**: Go 模块路径 `github.com/qizhi125/Sentinel-Flow`，GOROOT 自动检测

## 7. CI 流水线

位置：`.github/workflows/ci.yml`

- **build-and-test**: Ubuntu Release/Debug 矩阵 + sanitizer 变体
- **asan-ubsan**: AddressSanitizer + UndefinedBehaviorSanitizer
- C++ 测试: `ctest --output-on-failure`
- C++ 格式化: `clang-format --dry-run --Werror`
- Go 构建 + 测试: 非 sanitizer 作业中
- Go lint: `golangci-lint`

## 8. 常见问题

### `setcap` 不可用

```bash
# Debian/Ubuntu
sudo apt install libcap2-bin
# Fedora
sudo dnf install libcap
```

### 文件系统不支持扩展属性

`tmpfs`、`NFS` 等不支持 `setcap`。将二进制移至 `ext4`/`xfs` 文件系统。

### 重新编译后 `setcap` 失效

重新编译产生新 inode，需重新执行 `setcap`。

### SELinux/AppArmor 拦截

```bash
# 临时测试
sudo setenforce 0
# 或配置策略允许 cap_net_raw
```
