# Linux 权限提升机制

## 概述

Sentinel-Flow CLI 工具 (`sentinel-cli`) 在 Linux 平台上需要 `CAP_NET_RAW` 和 `CAP_NET_ADMIN` 权限才能通过原始套接字 (`AF_PACKET`) 或 AF_XDP 捕获网络流量。为了避免以 root 身份运行整个进程（可能引发安全风险或 Wayland/X11 显示问题），推荐使用 `setcap` 为二进制文件授予所需能力，使其以普通用户身份运行但具备网络捕获权限。

## 权限探测

工具启动时会尝试创建 `AF_PACKET` 原始套接字来检测是否已具备所需权限。若创建失败（通常返回 `EPERM`），将提示用户权限不足并给出提权指引。

## 推荐方案：使用 setcap 授予能力

为编译生成的二进制文件一次性赋予网络能力，后续无需 `sudo` 即可运行：

```bash
# 赋予捕获原始网络包和管理网络设备的能力
sudo setcap cap_net_raw,cap_net_admin=eip ./bin/sentinel-cli

# 验证能力已附加
getcap ./bin/sentinel-cli
```

之后直接运行即可：

```bash
./bin/sentinel-cli -i eth0 -r ./configs/rules.yaml
```

### 能力说明

| 能力            | 作用                                                         |
| --------------- | ------------------------------------------------------------ |
| `cap_net_raw`   | 允许使用原始套接字（`AF_PACKET`、`AF_XDP`）                  |
| `cap_net_admin` | 允许执行网络管理操作（设置混杂模式、BPF 过滤器等）           |
| `=eip`          | 设置有效（effective）、继承（inheritable）和允许（permitted）位 |

## 备选方案：使用 sudo 运行

若不方便使用 `setcap`（例如二进制文件所在文件系统不支持扩展属性），也可通过 `sudo` 直接运行：

```bash
sudo ./bin/sentinel-cli -i eth0 -r ./configs/rules.yaml
```

**注意**：在 Wayland 环境下，`sudo` 可能导致终端或图形会话异常（如无法访问显示服务），但对于纯 CLI 工具通常无影响。

## 提权失败或降级处理

若未授予能力且未使用 `sudo`，引擎将无法打开实时网卡。此时系统会输出警告，并自动进入**离线模式**，仅支持导入 PCAP 文件进行分析：

```bash
./bin/sentinel-cli --offline /path/to/capture.pcap
```

## 非 Linux 平台

在 macOS 或 Windows 上，由于不支持 `AF_PACKET`，实时捕获功能不可用。工具将强制以离线模式运行，仅能处理 PCAP 文件。

## 安全考量

- **最小权限原则**：仅授予必要的网络能力，避免完全 root 权限。
- **文件完整性**：`setcap` 将能力写入文件扩展属性，请确保可执行文件不被恶意篡改。
- **能力继承**：子进程不会自动继承父进程的能力，避免权限泄漏。

## 故障排查

### setcap 命令不存在

安装 `libcap` 工具包：

```bash
# Debian/Ubuntu
sudo apt install libcap2-bin

# Fedora
sudo dnf install libcap
```

### 文件系统不支持扩展属性

某些文件系统（如 tmpfs、NFS）可能不支持 `setcap`。请将二进制文件移动至支持的文件系统（如 ext4、xfs）再执行命令。

### 运行后仍提示权限不足

检查能力是否被正确附加：

```bash
getcap ./bin/sentinel-cli
# 应输出：./bin/sentinel-cli cap_net_raw,cap_net_admin=eip
```

若二进制文件被更新（重新编译），需重新执行 `setcap`。

### SELinux/AppArmor 拦截

强制访问控制系统可能阻止能力生效。可临时关闭 SELinux（`setenforce 0`）测试，或配置相应策略允许 `cap_net_raw`。

