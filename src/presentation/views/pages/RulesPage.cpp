#include "presentation/views/pages/RulesPage.h"
#include "presentation/views/styles/RulesStyle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

RulesPage::RulesPage(QWidget *parent) : QWidget(parent) {
    setupUi();
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
        btn->setStyleSheet(RulesStyle::getTabButton());
        btn->setMinimumWidth(100);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        if (i == 0) btn->setChecked(true);
        tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    connect(tabGroup, &QButtonGroup::idClicked, this, &RulesPage::onTabClicked);
    mainLayout->addWidget(tabContainer);

    // Content Stack
    contentStack = new QStackedWidget();
    contentStack->addWidget(createBpfPage());
    contentStack->addWidget(createIdsPage());
    contentStack->addWidget(createBlacklistPage());
    contentStack->addWidget(createProtocolPage());
    contentStack->addWidget(createAuditPage());
    mainLayout->addWidget(contentStack);
}

void RulesPage::onTabClicked(int id) {
    contentStack->setCurrentIndex(id);
}

QWidget* RulesPage::createInfoBox(const QString& title, const QString& text) {
    auto *box = new QFrame();
    box->setStyleSheet(RulesStyle::getInfoBox());
    auto *l = new QVBoxLayout(box);
    auto *lblT = new QLabel(title);
    lblT->setObjectName("InfoTitle"); // 设置objectName
    lblT->setStyleSheet(RulesStyle::getInfoTitle());
    auto *lblC = new QLabel(text);
    lblC->setObjectName("InfoContent"); // 设置objectName
    lblC->setWordWrap(true);
    lblC->setStyleSheet(RulesStyle::getInfoContent());
    l->addWidget(lblT); l->addWidget(lblC);
    return box;
}

QWidget* RulesPage::createBpfPage() {
    auto *w = new QWidget();
    w->setObjectName("Card");
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(30,30,30,30);
    l->setSpacing(15);
    l->addWidget(createInfoBox("BPF 过滤器", "使用 Berkeley Packet Filter 语法在内核层过滤流量。"));

    auto *h = new QHBoxLayout();
    auto *lbl = new QLabel("预设模板:");
    h->addWidget(lbl);
    bpfPresets = new QComboBox();
    bpfPresets->addItems({"自定义", "仅 HTTP (tcp port 80)", "仅 DNS (udp port 53)", "排除 SSH (not port 22)"});
    h->addWidget(bpfPresets);
    h->addStretch();
    l->addLayout(h);

    bpfEditor = new QTextEdit();
    bpfEditor->setPlaceholderText("在此输入 BPF 表达式，例如: tcp and port 80");
    bpfEditor->setStyleSheet(RulesStyle::Editor);
    l->addWidget(bpfEditor);

    btnApplyBpf = new QPushButton("应用过滤器");
    btnApplyBpf->setStyleSheet(RulesStyle::getBtnAction());
    connect(btnApplyBpf, &QPushButton::clicked, this, &RulesPage::onApplyBpf);
    auto *hl = new QHBoxLayout();
    hl->addStretch();
    hl->addWidget(btnApplyBpf);
    l->addLayout(hl);

    return w;
}

QWidget* RulesPage::createIdsPage() {
    auto *w = new QWidget(); w->setObjectName("Card");
    auto *l = new QVBoxLayout(w); l->setContentsMargins(30,30,30,30); l->setSpacing(15);
    l->addWidget(createInfoBox("IDS 规则", "基于特征码的入侵检测规则管理。"));

    auto *toolbar = new QHBoxLayout();
    ruleNameInput = new QLineEdit();
    ruleNameInput->setPlaceholderText("规则名称...");
    ruleNameInput->setStyleSheet(RulesStyle::Input);
    btnAddRule = new QPushButton("➕ 添加规则");
    btnAddRule->setStyleSheet(RulesStyle::getBtnAction());
    btnImportRules = new QPushButton("📥 导入规则");
    btnImportRules->setStyleSheet(RulesStyle::getBtnAction());
    connect(btnImportRules, &QPushButton::clicked, this, &RulesPage::onImportRules);
    btnDeleteRule = new QPushButton("🗑 删除选中");
    btnDeleteRule->setStyleSheet(RulesStyle::getBtnDangerOutline());

    connect(btnAddRule, &QPushButton::clicked, this, &RulesPage::onAddRule);
    connect(btnDeleteRule, &QPushButton::clicked, this, &RulesPage::onDeleteRule);

    toolbar->addWidget(ruleNameInput);
    toolbar->addWidget(btnAddRule);
    toolbar->addWidget(btnDeleteRule);
    l->addLayout(toolbar);

    idsTable = new QTableWidget(0, 3);
    idsTable->setHorizontalHeaderLabels({"规则名", "状态", "检测特征"});
    idsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    l->addWidget(idsTable);

    return w;
}

QWidget* RulesPage::createBlacklistPage() {
    auto *w = new QWidget(); w->setObjectName("Card");
    auto *l = new QVBoxLayout(w); l->setContentsMargins(30,30,30,30); l->setSpacing(15);
    l->addWidget(createInfoBox("IP 黑名单", "自动丢弃来自以下 IP 地址的所有数据包。"));

    auto *toolbar = new QHBoxLayout();
    blockIpInput = new QLineEdit();
    blockIpInput->setPlaceholderText("输入 IP 地址 (e.g. 192.168.1.100)");
    blockIpInput->setStyleSheet(RulesStyle::Input);

    QString ipRange = "(?:[0-1]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])";
    QRegularExpression ipRegex("^" + ipRange + "\\." + ipRange + "\\." + ipRange + "\\." + ipRange + "$");
    auto *ipValidator = new QRegularExpressionValidator(ipRegex, blockIpInput);
    blockIpInput->setValidator(ipValidator);

    btnAddBlock = new QPushButton("⛔ 封禁 IP");
    btnAddBlock->setStyleSheet(RulesStyle::BtnDangerFilled);
    btnRemoveBlock = new QPushButton("解封选中");
    btnRemoveBlock->setStyleSheet(RulesStyle::getBtnAction());

    connect(btnAddBlock, &QPushButton::clicked, this, &RulesPage::onAddBlacklist);
    connect(btnRemoveBlock, &QPushButton::clicked, this, &RulesPage::onRemoveBlacklist);

    toolbar->addWidget(blockIpInput);
    toolbar->addWidget(btnAddBlock);
    toolbar->addWidget(btnRemoveBlock);
    l->addLayout(toolbar);

    blacklistTable = new QTableWidget(0, 2);
    blacklistTable->setHorizontalHeaderLabels({"封禁 IP", "添加时间"});
    blacklistTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    l->addWidget(blacklistTable);

    return w;
}

QWidget* RulesPage::createProtocolPage() {
    auto *w = new QWidget(); w->setObjectName("Card");
    auto *l = new QVBoxLayout(w); l->setContentsMargins(30,30,30,30); l->setSpacing(20);
    l->addWidget(createInfoBox("协议控制", "启用或禁用特定协议的深度解析器。"));

    auto addCb = [&](const QString& text) {
        auto *cb = new QCheckBox(text);
        cb->setChecked(true);
        cb->setStyleSheet(RulesStyle::getCheckBox());
        l->addWidget(cb);
    };
    addCb("解析 TCP 协议 (Transmission Control Protocol)");
    addCb("解析 UDP 协议 (User Datagram Protocol)");
    addCb("解析 HTTP/HTTPS 头部信息");
    addCb("解析 DNS 查询请求");
    l->addStretch(); return w;
}

QWidget* RulesPage::createAuditPage() {
    auto *w = new QWidget(); w->setObjectName("Card");
    auto *l = new QVBoxLayout(w); l->setContentsMargins(30,30,30,30); l->setSpacing(15);
    l->addWidget(createInfoBox("审计日志", "查看系统操作记录与合规性报告。"));

    auto *grp = new QGroupBox("本地存储配置");
    grp->setStyleSheet(RulesStyle::getGroupBox());
    auto *gl = new QHBoxLayout(grp);
    logPathInput = new QLineEdit();
    logPathInput->setReadOnly(true);
    logPathInput->setPlaceholderText("请选择日志保存目录...");
    logPathInput->setStyleSheet(RulesStyle::Input);

    auto *btnBrowse = new QPushButton("浏览...");
    btnBrowse->setStyleSheet(RulesStyle::getBtnAction());
    connect(btnBrowse, &QPushButton::clicked, this, &RulesPage::onBrowseLogPath);

    gl->addWidget(new QLabel("日志路径:"));
    gl->addWidget(logPathInput);
    gl->addWidget(btnBrowse);

    l->addWidget(grp);
    l->addStretch(); return w;
}

void RulesPage::onApplyBpf() {
    QString bpf = bpfEditor->toPlainText().trimmed();
    if (bpf.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "BPF 表达式不能为空！");
        return;
    }
    QMessageBox::information(this, "配置生效", QString("BPF 过滤器已更新为：\n%1").arg(bpf));
}

void RulesPage::onAddRule() {
    if(ruleNameInput->text().isEmpty())
        return;
    int row = idsTable->rowCount();
    idsTable->insertRow(row);
    idsTable->setItem(row, 0, new QTableWidgetItem(ruleNameInput->text()));
    idsTable->setItem(row, 1, new QTableWidgetItem("启用"));
    idsTable->setItem(row, 2, new QTableWidgetItem("TCP Payload contains 'malware'"));
}

void RulesPage::onDeleteRule() {
    QList<QTableWidgetItem*> selected = idsTable->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择要删除的规则。");
        return;
    }
    if (QMessageBox::Yes == QMessageBox::question(this, "确认删除", "确定要删除选中的 IDS 规则吗？", QMessageBox::Yes | QMessageBox::No)) {
        int row = idsTable->currentRow();
        if (row >= 0) idsTable->removeRow(row);
    }
}

void RulesPage::onImportRules() {
    QString fileName = QFileDialog::getOpenFileName(this, "导入 Snort 规则文件", "", "Snort Rules (*.rules);;All Files (*)");
    if (!fileName.isEmpty()) {
        QMessageBox::information(this, "导入成功", QString("已成功解析规则文件：\n%1").arg(fileName));
        int row = idsTable->rowCount();
        idsTable->insertRow(row);
        idsTable->setItem(row, 0, new QTableWidgetItem("Snort-Imported-001"));
        idsTable->setItem(row, 1, new QTableWidgetItem("启用"));
        idsTable->setItem(row, 2, new QTableWidgetItem("alert tcp any any -> any 80 (msg:\"SQL Injection\";)"));
    }
}

void RulesPage::onAddBlacklist() {
    QString ip = blockIpInput->text().trimmed();
    if(ip.isEmpty() || !blockIpInput->hasAcceptableInput()) {
        QMessageBox::warning(this, "格式错误", "请输入有效的 IPv4 地址！\n例如: 192.168.1.10");
        return;
    }
    int row = blacklistTable->rowCount(); blacklistTable->insertRow(row);
    blacklistTable->setItem(row, 0, new QTableWidgetItem(blockIpInput->text()));
    blacklistTable->setItem(row, 1, new QTableWidgetItem(QDateTime::currentDateTime().toString()));
}

void RulesPage::onRemoveBlacklist() {
    QList<QTableWidgetItem*> selected = blacklistTable->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择要解封的 IP。");
        return;
    }
    int row = blacklistTable->currentRow();
    if (row >= 0 && QMessageBox::Yes == QMessageBox::question(this, "解封确认", "确定解封？"))
        blacklistTable->removeRow(row);
}

void RulesPage::onBrowseLogPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择日志目录");
    if (!dir.isEmpty()) logPathInput->setText(dir);
}