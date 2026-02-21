#include <QApplication>
#include <iostream>
#include <vector>
#include <string>
#include "presentation/views/MainWindow.h"
#include "engine/governance/AuditLogger.h"
#include "capture/impl/PcapCapture.h"
#include "engine/pipeline/PacketPipeline.h"
#include "common/queues/ThreadSafeQueue.h"
#include "styles/ThemeManager.h"

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argv, argv + argc);
    bool noGui = false;
    std::string interface = "";

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--no-gui") noGui = true;
        if (args[i] == "--interface" && i + 1 < args.size()) {
            interface = args[i + 1];
        }
    }

    if (noGui) {
        // CLI 模式 (静默运行)
        std::cout << "Starting Sentinel-Flow in CLI Mode..." << std::endl;

        // 初始化核心组件 (不依赖 QWidget)
        int workerCount = 2;
        std::vector<ThreadSafeQueue<RawPacket>*> queues;
        std::vector<PacketPipeline*> pipelines;

        for (int i = 0; i < workerCount; ++i) {
            auto* q = new ThreadSafeQueue<RawPacket>();
            queues.push_back(q);

            auto* pipe = new PacketPipeline();
            pipe->setInputQueue(q);
            pipelines.push_back(pipe);
            pipe->startPipeline();
        }

        PcapCapture::instance().init(queues);

        if (interface.empty()) {
            auto devs = PcapCapture::getDeviceList();
            if (!devs.empty()) interface = devs[0];
        }

        if (!interface.empty()) {
            PcapCapture::instance().start(interface);
            AuditLogger::instance().log("Engine started on " + interface, "SUCCESS");
        }

        std::cout << "Press Ctrl+C to stop..." << std::endl;
        while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }

        return 0;

    } else {
        // GUI 模式 (默认)
        QApplication app(argc, argv);

        qRegisterMetaType<ParsedPacket>("ParsedPacket");
        qRegisterMetaType<QVector<ParsedPacket>>("QVector<ParsedPacket>");
        qRegisterMetaType<Alert>("Alert");

        app.setApplicationName("Sentinel-Flow");
        app.setApplicationVersion("6.0.0");

        ThemeManager::applyTheme(app, true);

        MainWindow w;
        w.show();

        return app.exec();
    }
}