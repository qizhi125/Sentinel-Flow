#!/bin/bash
echo "=== 1. C++ 构建工具链验证 ==="
g++ --version | head -n 1
cmake --version | head -n 1
make --version | head -n 1

echo -e "\n=== 2. Go 环境与 CGO 验证 ==="
go version
go env CGO_ENABLED GOOS GOARCH

echo -e "\n=== 3. 核心依赖库验证 (Fedora) ==="
echo "[libpcap 状态]"
pkg-config --modversion libpcap || echo "缺失 libpcap"
rpm -qa | grep libpcap-devel || echo "缺失 libpcap-devel (C++编译必须)"

echo "[eBPF / XDP 开发包状态]"
rpm -qa | grep libbpf-devel || echo "缺失 libbpf-devel"
rpm -qa | grep kernel-devel | grep $(uname -r) || echo "缺失当前内核的 kernel-devel"

echo -e "\n=== 4. eBPF 编译工具链 (Clang/LLVM) ==="
clang --version | head -n 1 || echo "缺失 clang"
llc --version | grep "LLVM version" || echo "缺失 llvm"
bpftool version 2>/dev/null | head -n 1 || echo "缺失 bpftool"

echo -e "\n=== 5. 权限与系统能力验证 ==="
# 检查当前用户是否有执行 setcap 的权限，这对于免 root 抓包至关重要
which setcap >/dev/null && echo "setcap 工具已安装" || echo "缺失 setcap (libcap包)"
