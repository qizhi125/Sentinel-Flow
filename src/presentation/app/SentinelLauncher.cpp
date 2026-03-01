#include "SentinelLauncher.h"
#include "presentation/cli/CliEngineManager.h"
#include "presentation/views/MainWindow.h"
#include "presentation/views/styles/ThemeManager.h"
#include "common/types/NetworkTypes.h"
#include <QApplication>
#include <QCoreApplication>
#include <iostream>
#include <string>
#include <vector>
#include <csignal>

#ifdef __linux__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <net/ethernet.h>
    #include <unistd.h>
    #include <filesystem>
#endif

void SentinelLauncher::handleSignal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n\033[1;33m[!] 收到停止信号，正在安全回收底层资源 (Graceful Shutdown)...\033[0m\n";
        if (auto app = QCoreApplication::instance()) {
            app->quit();
        } else {
            std::exit(0);
        }
    }
}

int SentinelLauncher::run(int argc, char *argv[]) {
    std::signal(SIGINT, handleSignal);

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
        bool hasPermission = false;
        int testSock = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (testSock >= 0) {
            hasPermission = true;
            ::close(testSock);
        }

        if (!hasPermission) {
            std::cout << "\n\033[1;33m[⚠️ 权限环境检测 (Privilege Check)]\033[0m\n";
            std::cout << "当前进程缺乏底层网卡特权 (CAP_NET_RAW)。\n";
            std::cout << "若不提权，系统将默认进入【离线取证降级模式】，无法捕获实时流量。\n";
            std::cout << "👉 是否立刻通过 sudo 为本程序自动写入网卡嗅探权限并重载？\n";
            std::cout << "(输入 Y 同意提权，输入 N 拒绝并进入离线模式) [Y/n]: ";

            std::string authChoice;
            std::getline(std::cin, authChoice);

            if (authChoice.empty() || authChoice == "Y" || authChoice == "y") {
                std::cout << "\033[1;36m[+] 正在请求授权，请在下方输入当前用户的 sudo 密码：\033[0m\n";

                std::string absPath = std::filesystem::absolute(argv[0]).string();
                std::string cmd = "sudo setcap cap_net_raw,cap_net_admin=eip \"" + absPath + "\"";
                int ret = std::system(cmd.c_str());

                if (ret == 0) {
                    std::cout << "\033[1;32m[+] 授权成功！正在通知操作系统重载进程镜像以应用特权...\033[0m\n";

                    std::vector<char*> new_argv;
                    new_argv.push_back(const_cast<char*>(absPath.c_str()));
                    new_argv.push_back(isCliMode ? (char*)"--cli" : (char*)"--gui");

                    for (int i = 1; i < argc; ++i) {
                        std::string arg = argv[i];
                        if (arg != "--cli" && arg != "--gui") {
                            new_argv.push_back(argv[i]);
                        }
                    }
                    new_argv.push_back(nullptr);

                    execv(absPath.c_str(), new_argv.data());

                    std::cerr << "❌ 进程重载失败！" << std::endl;
                    std::exit(1);
                } else {
                    std::cout << "\033[1;31m[!] 授权失败或被取消。程序将以【无权限离线模式】继续启动。\033[0m\n";
                }
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
        qRegisterMetaType<QVector<ParsedPacket>>("QVector<ParsedPacket>");
        qRegisterMetaType<Alert>("Alert");

        app.setApplicationName("Sentinel-Flow");
        app.setApplicationVersion("6.0.0");

        ThemeManager::applyTheme(app, true);

        std::cout << "\033[1;32m[+] 权限验证通过，正在渲染主控大屏...\033[0m\n";
        MainWindow w;
        w.show();

        return app.exec();
    }
}