#include "presentation/views/pages/RulesPage.h"
#include "presentation/views/pages/rules/IdsRulesTab.h"
#include "presentation/views/components/UIFactory.h"
#include "engine/context/DatabaseManager.h"
#include "engine/flow/SecurityEngine.h"
#include "engine/flow/PacketParser.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

RulesPage::RulesPage(QWidget *parent) : QWidget(parent) {
    setupUi();
    loadPersistedData();
}

void RulesPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(0);

    auto *tabContainer = new QWidget();
    auto *tabLayout = new QHBoxLayout(tabContainer);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(10);
    tabGroup = new QButtonGroup(this);

    QStringList tabs = {"BPF 过滤器", "IDS 规则", "黑名单", "协议控制", "审计"};
    for (int i = 0; i < tabs.size(); ++i) {
        auto *btn = new QPushButton(tabs[i]);
        btn->setCheckable(true);
        btn->setProperty("type", "tab");
        btn->setMinimumWidth(100);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (i == 1) btn->setChecked(true);
        tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    mainLayout->addWidget(tabContainer);

    contentStack = new QStackedWidget();
    contentStack->addWidget(createBpfPage());

    contentStack->addWidget(new IdsRulesTab(this));

    contentStack->addWidget(createBlacklistPage());
    contentStack->addWidget(createProtocolPage());
    contentStack->addWidget(createAuditPage());
    contentStack->setCurrentIndex(1);
    connect(tabGroup, &QButtonGroup::idClicked, this, &RulesPage::onTabClicked);

    mainLayout->addWidget(contentStack);
}

void RulesPage::loadPersistedData() {
    auto bl = DatabaseManager::instance().loadBlacklist();
    blacklistTable->setRowCount(0);
    for (const auto& item : bl) {
        int row = blacklistTable->rowCount();
        blacklistTable->insertRow(row);
        blacklistTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(item.first)));
        blacklistTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(item.second)));

        uint32_t ipInt = ntohl(inet_addr(item.first.c_str()));
        SecurityEngine::instance().blockIp(ipInt);
    }
}

void RulesPage::onTabClicked(int id) {
    contentStack->setCurrentIndex(id);
}

QWidget* RulesPage::createBlacklistPage() {
    auto *w = new QWidget();
    auto *mainLayout = new QVBoxLayout(w);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    mainLayout->addWidget(UIFactory::createInfoBox("IP 黑名单", "自动在底层拦截来自恶意源的流量。建议定期清理陈旧条目以优化整体性能。"), 0);

    auto *contentVSplitter = new QSplitter(Qt::Vertical);
    contentVSplitter->setHandleWidth(1);

    auto *topControl = new QFrame();
    topControl->setObjectName("Card");
    auto *topLayout = new QHBoxLayout(topControl);

    blockIpInput = new QLineEdit();
    blockIpInput->setPlaceholderText("输入要封禁的 IPv4 地址 (例如 192.168.1.100)...");

    QString ipRange = "(?:[0-1]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])";
    QRegularExpression ipRegex("^" + ipRange + "\\." + ipRange + "\\." + ipRange + "\\." + ipRange + "$");
    blockIpInput->setValidator(new QRegularExpressionValidator(ipRegex, blockIpInput));

    btnAddBlock = new QPushButton("⛔ 立即封禁");
    btnAddBlock->setProperty("type", "danger");

    btnRemoveBlock = new QPushButton("解封选中");
    btnRemoveBlock->setProperty("type", "primary");

    auto *btnClearBlacklist = new QPushButton("一键清空");
    btnClearBlacklist->setProperty("type", "outline-danger");

    connect(btnAddBlock, &QPushButton::clicked, this, &RulesPage::onAddBlacklist);
    connect(btnRemoveBlock, &QPushButton::clicked, this, &RulesPage::onRemoveBlacklist);
    connect(btnClearBlacklist, &QPushButton::clicked, [this](){
        if(QMessageBox::Yes == QMessageBox::question(this, "严重警告", "确定解封所有被封禁的 IP 吗？")) {
            blacklistTable->setRowCount(0);
            // 这里你可以根据需要追加 DatabaseManager 的 clearBlacklist 调用
        }
    });

    topLayout->addWidget(new QLabel("目标 IP:"));
    topLayout->addWidget(blockIpInput, 1);
    topLayout->addWidget(btnAddBlock);
    topLayout->addWidget(btnRemoveBlock);
    topLayout->addWidget(btnClearBlacklist);

    contentVSplitter->addWidget(topControl);

    blacklistTable = new QTableWidget(0, 2);
    blacklistTable->setHorizontalHeaderLabels({"封禁 IP 地址", "添加时间 / 拦截原因"});
    blacklistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    blacklistTable->setSelectionMode(QAbstractItemView::SingleSelection);
    blacklistTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    blacklistTable->setShowGrid(false);
    blacklistTable->verticalHeader()->setVisible(false);

    blacklistTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    blacklistTable->setColumnWidth(0, 250);
    blacklistTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    contentVSplitter->addWidget(blacklistTable);

    contentVSplitter->setStretchFactor(0, 2);
    contentVSplitter->setStretchFactor(1, 8);

    mainLayout->addWidget(contentVSplitter, 1);

    return w;
}

void RulesPage::onAddBlacklist() {
    QString ip = blockIpInput->text().trimmed();
    if(ip.isEmpty() || !blockIpInput->hasAcceptableInput()) {
        QMessageBox::warning(this, "格式错误", "请输入有效的 IPv4 地址！");
        return;
    }
    QString reason = "手动封禁";
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    DatabaseManager::instance().saveBlacklist(ip.toStdString(), reason.toStdString());

    uint32_t ipInt = ntohl(inet_addr(ip.toStdString().c_str()));
    SecurityEngine::instance().blockIp(ipInt);

    int row = blacklistTable->rowCount();
    blacklistTable->insertRow(row);
    blacklistTable->setItem(row, 0, new QTableWidgetItem(ip));
    blacklistTable->setItem(row, 1, new QTableWidgetItem(timeStr + " / " + reason));

    blockIpInput->clear();
}

void RulesPage::onRemoveBlacklist() {
    if (blacklistTable->selectedItems().isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择要解封的 IP。");
        return;
    }
    int row = blacklistTable->currentRow();
    QString ip = blacklistTable->item(row, 0)->text();

    if (QMessageBox::Yes == QMessageBox::question(this, "解封确认", QString("确定要从引擎中解封 IP: %1 吗？").arg(ip))) {
        DatabaseManager::instance().deleteBlacklist(ip.toStdString());

        uint32_t ipInt = ntohl(inet_addr(ip.toStdString().c_str()));
        SecurityEngine::instance().unblockIp(ipInt);

        blacklistTable->removeRow(row);
    }
}

QWidget* RulesPage::createProtocolPage() {
    auto *w = new QWidget();
    auto *mainLayout = new QVBoxLayout(w);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    mainLayout->addWidget(UIFactory::createInfoBox("协议控制中心", "深度解析引擎的神经中枢。通过预设场景一键切换引擎负载，或手动精细控制特定解析器。"), 0);

    auto *contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->setHandleWidth(1);

    auto *leftCard = new QFrame();
    leftCard->setObjectName("Card");
    auto *vLeft = new QVBoxLayout(leftCard);

    auto *lblLeftTitle = new QLabel("✅ 深度检测引擎 (DPI) 状态");
    lblLeftTitle->setStyleSheet("font-weight: bold; font-size: 14px; margin-bottom: 5px;");
    vLeft->addWidget(lblLeftTitle);

    QCheckBox *cbTcp, *cbUdp, *cbHttp, *cbTls, *cbIcmp;

    auto addAdvancedCb = [&](const QString& text, const QString& desc, bool checked, QCheckBox*& outCb) {
        auto *itemW = new QWidget();
        auto *itemL = new QVBoxLayout(itemW);
        itemL->setContentsMargins(5, 5, 5, 5);
        itemL->setSpacing(2);

        outCb = new QCheckBox(text);
        outCb->setChecked(checked);
        outCb->setStyleSheet("font-weight: bold; font-size: 16px;");

        auto *lblDesc = new QLabel(desc);
        lblDesc->setStyleSheet("color: #888; font-size: 14px; margin-left: 22px;");

        itemL->addWidget(outCb);
        itemL->addWidget(lblDesc);
        vLeft->addWidget(itemW);
    };

    addAdvancedCb("TCP 数据流重组引擎", "跟踪 TCP 状态机，是所有 HTTP/TLS 深度检测的基础。", PacketParser::ENABLE_TCP, cbTcp);
    addAdvancedCb("UDP 会话追踪引擎", "追踪 UDP 五元组存活状态，用于 DNS 等协议分析。", PacketParser::ENABLE_UDP, cbUdp);
    addAdvancedCb("HTTP 载荷解析器 (L7)", "提取 URI、Method 特征。万兆洪峰下建议关闭以节省 CPU。", PacketParser::ENABLE_HTTP, cbHttp);
    addAdvancedCb("TLS ClientHello 探测", "无损提取 SNI 域名，极低性能损耗，用于发现恶意远控。", PacketParser::ENABLE_TLS, cbTls);
    addAdvancedCb("ICMP 诊断特征解析", "用于检测 Ping 隧道和潜在的网络踩点扫描行为。", PacketParser::ENABLE_ICMP, cbIcmp);
    vLeft->addStretch();

    auto *rightCard = new QFrame();
    rightCard->setObjectName("Card");
    auto *vRight = new QVBoxLayout(rightCard);

    auto *lblRightTitle = new QLabel("⚖️ 架构师性能平衡建议");
    lblRightTitle->setStyleSheet("font-weight: bold; font-size: 14px; margin-bottom: 10px;");
    vRight->addWidget(lblRightTitle);

    auto *hPreset = new QHBoxLayout();
    hPreset->addWidget(new QLabel("预设运行场景:"));
    QComboBox *cbPreset = new QComboBox();
    cbPreset->addItems({
        "🛡️ 全量深度解析模式 (推荐 1Gbps 以内)",
        "⚡ 高性能降级模式 (丢弃 HTTP, 推荐 10Gbps)",
        "🎯 极端抗压模式 (仅 L3/L4 状态追踪)",
        "⚙️ 自定义配置 (Custom)"
    });
    cbPreset->setFixedHeight(32);
    hPreset->addWidget(cbPreset, 1);
    vRight->addLayout(hPreset);

    auto *hintBox = new QLabel();
    hintBox->setWordWrap(true);
    hintBox->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    hintBox->setStyleSheet("line-height: 1.6; font-size: 13px; margin-top: 10px; padding: 10px; background-color: rgba(13, 110, 253, 0.05); border-radius: 4px; border: 1px solid rgba(13, 110, 253, 0.2);");
    vRight->addWidget(hintBox);
    vRight->addStretch();

    auto updateEngineAndHint = [=]() {
        PacketParser::ENABLE_TCP = cbTcp->isChecked();
        PacketParser::ENABLE_UDP = cbUdp->isChecked();
        PacketParser::ENABLE_HTTP = cbHttp->isChecked();
        PacketParser::ENABLE_TLS = cbTls->isChecked();
        PacketParser::ENABLE_ICMP = cbIcmp->isChecked();

        int idx = cbPreset->currentIndex();
        if (idx == 0) {
            hintBox->setText("<b>💡 全量深度解析模式：</b><br>所有 DPI 引擎满载运行。拥有最高的威胁可见性，适用于企业出口或千兆级 (1Gbps) 核心交换机镜像口。");
        } else if (idx == 1) {
            hintBox->setText("<b>💡 高性能降级模式：</b><br>已关闭极其消耗 CPU 的 HTTP 载荷重组，保留 TLS SNI 提取。吞吐量提升约 40%，适用于万兆 (10Gbps) 洪峰环境。");
        } else if (idx == 2) {
            hintBox->setText("<b style='color:#D32F2F;'>⚠️ 极端抗压模式：</b><br>已关闭所有 L7 载荷解析。系统退化为纯粹的 L3/L4 状态防火墙。绝大多数高级 IDS 规则将失效！仅在面临超级 DDoS 攻击保全宿主机时使用。");
        } else {
            hintBox->setText("<b>⚙️ 自定义模式：</b><br>您已手动修改了引擎开关。请注意，关闭 TCP 重组将导致几乎所有 Web 防护策略瘫痪。");
        }
    };

    QObject::connect(cbPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        QSignalBlocker b1(cbTcp), b2(cbUdp), b3(cbHttp), b4(cbTls), b5(cbIcmp); // 防止触发复选框的回调
        if (index == 0) {
            cbTcp->setChecked(true); cbUdp->setChecked(true); cbHttp->setChecked(true); cbTls->setChecked(true); cbIcmp->setChecked(true);
        } else if (index == 1) {
            cbTcp->setChecked(true); cbUdp->setChecked(true); cbHttp->setChecked(false); cbTls->setChecked(true); cbIcmp->setChecked(true);
        } else if (index == 2) {
            cbTcp->setChecked(true); cbUdp->setChecked(true); cbHttp->setChecked(false); cbTls->setChecked(false); cbIcmp->setChecked(false);
        }
        updateEngineAndHint();
    });

    auto onCbToggled = [=](bool) {
        QSignalBlocker b(cbPreset);
        cbPreset->setCurrentIndex(3);
        updateEngineAndHint();
    };
    QObject::connect(cbTcp, &QCheckBox::toggled, onCbToggled);
    QObject::connect(cbUdp, &QCheckBox::toggled, onCbToggled);
    QObject::connect(cbHttp, &QCheckBox::toggled, onCbToggled);
    QObject::connect(cbTls, &QCheckBox::toggled, onCbToggled);
    QObject::connect(cbIcmp, &QCheckBox::toggled, onCbToggled);

    updateEngineAndHint();

    contentSplitter->addWidget(leftCard);
    contentSplitter->addWidget(rightCard);
    contentSplitter->setStretchFactor(0, 4);
    contentSplitter->setStretchFactor(1, 6);
    mainLayout->addWidget(contentSplitter, 1);

    return w;
}

QWidget* RulesPage::createBpfPage() {
    auto *w = new QWidget();
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 20, 20, 20);
    l->setSpacing(15);
    l->addWidget(UIFactory::createInfoBox("BPF 过滤器", "在内核层 (Kernel Space) 丢弃无关数据包，这是最高效的流量过滤方式。"), 0);

    auto *card = new QWidget(); card->setObjectName("Card");
    auto *cardLayout = new QVBoxLayout(card);

    auto *toolbar = new QHBoxLayout();
    bpfPresets = new QComboBox();
    bpfPresets->addItems({"自定义表达式...", "仅抓取 TCP (tcp)", "过滤本地回环 (not loopback)", "仅抓取 HTTP (tcp port 80)"});
    bpfPresets->setFixedWidth(250);

    btnApplyBpf = new QPushButton("✅ 应用过滤器");
    btnApplyBpf->setProperty("type", "primary");
    connect(btnApplyBpf, &QPushButton::clicked, this, &RulesPage::onApplyBpf);

    toolbar->addWidget(new QLabel("预设配置:"));
    toolbar->addWidget(bpfPresets);
    toolbar->addStretch();
    toolbar->addWidget(btnApplyBpf);
    cardLayout->addLayout(toolbar);

    bpfEditor = new QTextEdit();
    bpfEditor->setPlaceholderText("在此输入标准的 Berkeley Packet Filter 表达式...\n例如: tcp port 80 or udp port 53");
    bpfEditor->setStyleSheet("font-family: monospace; font-size: 14px;");
    cardLayout->addWidget(bpfEditor);

    l->addWidget(card, 1);
    return w;
}

void RulesPage::onApplyBpf() {
    QString bpf = bpfEditor->toPlainText().trimmed();
    QMessageBox::information(this, "过滤器", "BPF 规则已下发至内核层 (Mock): " + (bpf.isEmpty() ? "None" : bpf));
}

QWidget* RulesPage::createAuditPage() {
    auto *w = new QWidget();
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(20, 20, 20, 20);
    l->setSpacing(15);
    l->addWidget(UIFactory::createInfoBox("系统审计", "配置取证 PCAP 文件的存储路径与告警日志的自动轮转。"), 0);

    auto *card = new QWidget(); card->setObjectName("Card");
    auto *cardLayout = new QVBoxLayout(card);

    auto *pathLayout = new QHBoxLayout();
    logPathInput = new QLineEdit();
    logPathInput->setText("./evidences/");
    logPathInput->setReadOnly(true);
    btnBrowse = new QPushButton("📂 更改路径");
    connect(btnBrowse, &QPushButton::clicked, this, &RulesPage::onBrowseLogPath);

    pathLayout->addWidget(new QLabel("恶意流量取证保存路径:"));
    pathLayout->addWidget(logPathInput, 1);
    pathLayout->addWidget(btnBrowse);
    cardLayout->addLayout(pathLayout);

    cardLayout->addWidget(new QCheckBox("启用日志自动轮转 (按天)"));
    cardLayout->addWidget(new QCheckBox("超过 5GB 自动覆盖旧的取证文件"));

    cardLayout->addStretch();
    l->addWidget(card, 1);
    return w;
}

void RulesPage::onBrowseLogPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择日志目录", QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) logPathInput->setText(dir);
}

void RulesPage::onThemeChanged() {
}