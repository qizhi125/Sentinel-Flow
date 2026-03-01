# Linux 权限提升机制

## 概述

Sentinel-Flow 在 Linux 平台上需要 `CAP_NET_RAW` 和 `CAP_NET_ADMIN` 权限才能通过原始套接字 (`AF_PACKET`) 捕获网络流量。为了避免以 root 身份运行整个 GUI 应用（这会引发 X11/Wayland 显示服务拒绝连接等问题），系统实现了一种**动态提权**机制：启动时探测权限，若不足则通过 `setcap` 为可执行文件授予所需能力，然后通过 `execv` 重新加载自身，以普通用户身份运行但具备网络捕获权限。

## 权限探测

```cpp
    #ifdef __linux__
        int testSock = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (testSock >= 0) {
            hasPermission = true;
            ::close(testSock);
        }
    #endif
```

- 尝试创建 `AF_PACKET` 原始套接字。
- 若成功，说明当前进程已具备 `CAP_NET_RAW`；若失败（通常是 `EPERM`），则需提权。

## 提权流程

### 用户交互

若权限不足，控制台输出提示：

```cpp
    std::cout << "\n\033[1;33m[⚠️ 权限环境检测 (Privilege Check)]\033[0m\n";
    std::cout << "当前进程缺乏底层网卡特权 (CAP_NET_RAW)。\n";
    std::cout << "若不提权，系统将默认进入【离线取证降级模式】，无法捕获实时流量。\n";
    std::cout << "👉 是否立刻通过 sudo 为本程序自动写入网卡嗅探特权并重载？\n";
    std::cout << "(输入 Y 同意提权，输入 N 拒绝并进入离线模式) [Y/n]: ";
```

用户输入决定后续行为。

### 执行提权

若用户同意，执行以下步骤：

```cpp
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程执行 setcap
        execlp("sudo", "sudo", "setcap", "cap_net_raw,cap_net_admin=eip",
               absPath.c_str(), (char*)NULL);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // 提权成功，准备重载进程
        }
    }
```

- 使用 `fork()` 创建子进程。
- 子进程调用 `sudo setcap cap_net_raw,cap_net_admin=eip <可执行文件路径>` 为二进制文件添加能力。
- `setcap` 命令将能力写入文件扩展属性，后续运行该文件时自动获得对应能力。
- 父进程等待子进程完成，若成功则进入重载阶段。

### 进程重载

```cpp
// 构建新命令行参数
    std::vector<std::string> args;
    args.push_back(absPath);
    // 确保包含模式参数（--gui 或 --cli）
    bool hasMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cli" || a == "--gui") {
            hasMode = true;
            args.push_back(a);
        } else {
            args.push_back(a);
        }
    }
    if (!hasMode) {
        args.insert(args.begin() + 1, isCliMode ? "--cli" : "--gui");
    }
    // 转换为 char* 数组
    std::vector<char*> cargv;
    for (auto &s : args) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    // 重载
    execv(absPath.c_str(), cargv.data());
```

- 收集原始命令行参数，确保传递正确的模式 (`--gui` 或 `--cli`)。
- 调用 `execv` 用新的进程映像替换当前进程。
- 新进程启动时，由于可执行文件已具备能力，将成功创建原始套接字。

### 降级处理

若用户拒绝提权或提权失败，系统输出提示：

```cpp
    std::cout << "\033[1;33m[!] 用户主动跳过授权。系统按原流程以离线模式继续启动...\033[0m\n";
```

之后应用程序以**离线模式**启动，跳过物理网卡捕获，仅支持导入 PCAP 文件进行离线分析。

## 非 Linux 平台处理

```cpp
    #else
        std::cout << "\n\033[1;34m[🌐 跨平台环境检测 (Cross-Platform Check)]\033[0m\n";
        std::cout << "检测到非 Linux 内核环境。实时网卡嗅探 (Live Capture) 功能已被禁用。\n";
        std::cout << "系统将强制进入【纯离线分析模式 (Offline Forensic Mode)】。\n";
    #endif
```

- 在 macOS 或 Windows 上，由于不支持 `AF_PACKET`，直接进入离线模式。
- 用户仍可通过 CLI 或 GUI 加载 PCAP 文件进行分析。

## 能力说明

| 能力 | 作用 |
|------|------|
| `cap_net_raw` | 允许使用原始套接字（如 `AF_PACKET`） |
| `cap_net_admin` | 允许执行网络管理操作（如设置混杂模式、BPF 过滤器） |
| `=eip` | 设置有效（effective）、继承（inheritable）和允许（permitted）位 |

## 安全考量

- **最小权限原则**：仅授予必要的网络能力，不赋予完全 root 权限。
- **避免 root 运行 GUI**：以普通用户身份运行，避免显示服务问题。
- **可执行文件路径完整性**：使用 `std::filesystem::canonical` 解析绝对路径，防止路径劫持。
- **重载后保持参数**：确保用户选择的模式（GUI/CLI）正确传递。

## 故障排查

### 提权失败常见原因

- **sudo 未配置无密码权限**：`setcap` 需要 sudo 权限，若用户未输入正确密码或 sudoers 配置不当，提权失败。
- **文件系统不支持扩展属性**：某些文件系统（如 tmpfs）可能不支持 `setcap`，需确保可执行文件在支持扩展属性的文件系统上（如 ext4、xfs）。
- **SELinux/AppArmor 拦截**：强制访问控制可能阻止 `setcap` 或应用程序获取能力。

### 降级后的功能

- 仍可正常使用 GUI/CLI 界面，查看统计、管理规则。
- 离线取证功能完整，可加载 PCAP 文件进行深度解析和告警检测。

## 使用示例

```bash
    # 直接运行，系统会自动检测并请求提权
    ./SentinelApp
    
    # 跳过交互，直接指定模式（仍会检测权限）
    ./SentinelApp --gui
    ./SentinelApp --cli
```

---
