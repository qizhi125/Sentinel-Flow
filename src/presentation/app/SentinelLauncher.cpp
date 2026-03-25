#include "presentation/app/SentinelLauncher.h"
#include "presentation/cli/CliEngineManager.h"
#include "presentation/views/MainWindow.h"
#include "presentation/views/styles/ThemeManager.h"
#include "common/types/NetworkTypes.h"
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <atomic>
#include <sys/wait.h>

#ifdef __linux__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <net/ethernet.h>
    #include <unistd.h>
    #include <filesystem>
#endif

static std::atomic<bool> g_shutdownRequested{false};

void SentinelLauncher::handleSignal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_shutdownRequested.store(true, std::memory_order_release);
    }
}

int SentinelLauncher::run(int argc, char *argv[]) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    bool isCliMode = false;
    bool skipMenu = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli") { isCliMode = true; skipMenu = true; }
        else if (arg == "--gui") { isCliMode = false; skipMenu = true; }
    }

    if (!skipMenu) {
        std::cout << "\033[1;36m=========================================================\n";
        std::cout << "    Sentinel-Flow v1.0 启动引导 (Boot Menu)\n";
        std::cout << "=========================================================\033[0m\n";
        std::cout << "  [1] 启动图形化管理控制台 (GUI Mode - 需要 X11/Wayland)\n";
        std::cout << "  [2] 启动高性能终端监控流 (CLI Mode - 适合 SSH/服务器)\n";
        std::cout << "---------------------------------------------------------\n";
        std::cout << "请选择运行模式 [1/2, 默认 1]: ";

        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "2" || choice == "cli" || choice == "c") isCliMode = true;
    }

    #ifdef __linux__
        // 🚀 核心修复：eBPF/AF_XDP 必须要求真实的 root 权限，仅仅 cap_net_raw 不再够用
        bool hasPermission = (geteuid() == 0);

        if (!hasPermission && !skipMenu) {
            std::cout << "\n\033[1;33m[⚠️ 权限环境检测 (Privilege Check)]\033[0m\n";
            std::cout << "当前进程非 root 用户。AF_XDP/eBPF 零拷贝引擎需要锁定物理内存与内核级挂载权限。\n";
            std::cout << "若不提权，系统将默认进入【离线取证降级模式】，无法捕获实时流量。\n";
            std::cout << "👉 是否立刻通过 sudo 以 root 身份重启本程序？\n";
            std::cout << "(输入 Y 同意提权，输入 N 拒绝并进入离线模式) [Y/n]: ";

            std::string authChoice;
            std::getline(std::cin, authChoice);

            if (authChoice.empty() || authChoice == "Y" || authChoice == "y") {
                std::string absPath;
                try {
                    absPath = std::filesystem::canonical(argv[0]).string();
                } catch (...) {
                    absPath = argv[0];
                }

                // 组装 sudo 的执行参数，无缝继承用户刚才选择的模式
                std::vector<char*> cargv;
                cargv.push_back(const_cast<char*>("sudo"));
                cargv.push_back(const_cast<char*>(absPath.c_str()));
                for (int i = 1; i < argc; ++i) {
                    cargv.push_back(argv[i]);
                }
                // 确保自动追加模式标志，避免 sudo 重启后再次弹菜单
                cargv.push_back(const_cast<char*>(isCliMode ? "--cli" : "--gui"));
                cargv.push_back(nullptr);

                std::cout << "\033[1;36m[+] 正在请求 sudo 授权，请在下方输入密码：\033[0m\n";
                execvp("sudo", cargv.data());

                // 如果 execvp 失败才会走到这里
                perror("execvp sudo");
                std::exit(1);

            } else {
                std::cout << "\033[1;33m[!] 用户主动跳过授权。系统按原流程以离线模式继续启动...\033[0m\n";
            }
            std::cout << "---------------------------------------------------------\n";
        }
    #else
        std::cout << "\n\033[1;34m[🌐 跨平台环境检测 (Cross-Platform Check)]\033[0m\n";
        std::cout << "检测到非 Linux 内核环境。实时网卡嗅探 (Live Capture) 功能已被禁用。\n";
        std::cout << "系统将强制进入【纯离线分析模式 (Offline Forensic Mode)】。\n";
        std::cout << "您可以在该模式下导入、解析并审计海量 PCAP 格式的流量证据文件。\n";
        std::cout << "---------------------------------------------------------\n";
    #endif

    if (isCliMode) {
        QCoreApplication app(argc, argv);
        std::cout << "\033[1;32m[+] 权限验证通过，加载终端守护进程...\033[0m\n";
        CliEngineManager cliManager;
        cliManager.start();

        QTimer shutdownTimer;
        QObject::connect(&shutdownTimer, &QTimer::timeout, [&app]() {
            if (g_shutdownRequested.load(std::memory_order_acquire)) {
                std::cout << "\n\033[1;33m[!] 收到停止信号，正在安全回收底层资源 (Graceful Shutdown)...\033[0m\n";
                app.quit();
            }
        });
        shutdownTimer.start(100);

        return app.exec();
    } else {
        qputenv("QT_QPA_PLATFORM", "xcb");
        qputenv("QT_QPA_PLATFORMTHEME", "null");
        qputenv("QT_STYLE_OVERRIDE", "fusion");
        qputenv("QT_ACCESSIBILITY", "0");
        qputenv("GTK_THEME", "Adwaita:light");
        qputenv("NO_AT_BRIDGE", "1");

        QApplication::setDesktopSettingsAware(false);

        QApplication app(argc, argv);

        app.setStyle("fusion");

        qRegisterMetaType<ParsedPacket>("ParsedPacket");
        qRegisterMetaType<QSharedPointer<QVector<ParsedPacket>>>("QSharedPointer<QVector<ParsedPacket>>");
        qRegisterMetaType<Alert>("Alert");

        app.setApplicationName("Sentinel-Flow");
        app.setApplicationVersion("1.0.0");

        ThemeManager::applyTheme(app, true);

        std::cout << "\033[1;32m[+] 权限验证通过，正在渲染主控大屏...\033[0m\n";
        MainWindow w;
        w.show();

        QTimer shutdownTimer;
        QObject::connect(&shutdownTimer, &QTimer::timeout, [&app]() {
            if (g_shutdownRequested.load(std::memory_order_acquire)) {
                std::cout << "\n\033[1;33m[!] 收到停止信号，正在安全回收底层资源 (Graceful Shutdown)...\033[0m\n";
                app.quit();
            }
        });
        shutdownTimer.start(100);

        return app.exec();
    }
}