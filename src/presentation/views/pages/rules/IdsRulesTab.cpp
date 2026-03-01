#include "presentation/views/components/UIFactory.h"
#include "engine/context/DatabaseManager.h"
#include "engine/flow/SecurityEngine.h"
#include "IdsRulesTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDateTime>

IdsRulesTab::IdsRulesTab(QWidget *parent) : QWidget(parent) {
    setupUi();
    loadPersistedData();
}

void IdsRulesTab::setupUi() {
    auto *mainLayout = new QVBoxLayout(this); 
    mainLayout->setContentsMargins(0, 0, 0, 0); 
    mainLayout->setSpacing(15);

    mainLayout->addWidget(UIFactory::createInfoBox("策略指挥中心", "管理高性能检测规则、黑名单情报与威胁等级映射。"));

    auto *contentVSplitter = new QSplitter(Qt::Vertical, this);
    contentVSplitter->setHandleWidth(1);
    contentVSplitter->setChildrenCollapsible(false);

    auto *topCardContainer = new QWidget(this);
    auto *topCardLayout = new QHBoxLayout(topCardContainer);
    topCardLayout->setContentsMargins(0, 5, 0, 5);
    topCardLayout->setSpacing(15);
    
    cardTotalRules = new StatCard("活跃检测引擎数", "0", StatCard::Normal);
    cardHighThreats = new StatCard("高危/严重策略数", "0", StatCard::Danger);
    cardIntercepted = new StatCard("已拦截风暴特征", "0", StatCard::Warning);
    topCardLayout->addWidget(cardTotalRules);
    topCardLayout->addWidget(cardHighThreats);
    topCardLayout->addWidget(cardIntercepted);
    contentVSplitter->addWidget(topCardContainer);

    auto *bottomHSplitter = new QSplitter(Qt::Horizontal);
    bottomHSplitter->setHandleWidth(1);
    bottomHSplitter->setChildrenCollapsible(false);

    auto *leftNavContainer = new QWidget(); 
    leftNavContainer->setObjectName("Card");
    auto *leftNavLayout = new QVBoxLayout(leftNavContainer);
    leftNavLayout->setContentsMargins(15, 15, 15, 15);
    
    auto *lblLeftTitle = new QLabel("⚖️ 维度过滤");
    lblLeftTitle->setStyleSheet("font-weight: bold; margin-bottom: 5px;");
    leftNavLayout->addWidget(lblLeftTitle);

    ruleFilterTree = new QTreeWidget();
    ruleFilterTree->setHeaderHidden(true);
    ruleFilterTree->setStyleSheet("QTreeWidget { border: none; background: transparent; }");

    new QTreeWidgetItem(ruleFilterTree, {"🌟 全部规则列表"});
    auto *itemSource = new QTreeWidgetItem(ruleFilterTree, {"📂 策略来源"});
    new QTreeWidgetItem(itemSource, {"本地自定义规则"});
    new QTreeWidgetItem(itemSource, {"Snort 社区同步"});
    auto *itemLevel = new QTreeWidgetItem(ruleFilterTree, {"🚨 威胁等级"});
    new QTreeWidgetItem(itemLevel, {"Critical (严重)"});
    new QTreeWidgetItem(itemLevel, {"High (高危)"});
    new QTreeWidgetItem(itemLevel, {"Medium (中危)"});
    new QTreeWidgetItem(itemLevel, {"Low / Info (低危/信息)"});
    
    ruleFilterTree->expandAll();
    connect(ruleFilterTree, &QTreeWidget::itemClicked, this, &IdsRulesTab::onFilterTreeClicked);
    leftNavLayout->addWidget(ruleFilterTree);
    bottomHSplitter->addWidget(leftNavContainer);

    auto *rightDataContainer = new QWidget(); 
    rightDataContainer->setObjectName("Card");
    auto *rightDataLayout = new QVBoxLayout(rightDataContainer);
    rightDataLayout->setContentsMargins(15, 15, 15, 15);

    auto *toolbar = new QHBoxLayout();
    ruleNameInput = new QLineEdit(); ruleNameInput->setPlaceholderText("规则描述...");
    rulePatternInput = new QLineEdit(); rulePatternInput->setPlaceholderText("特征码 (Pattern)");
    btnAddRule = new QPushButton("➕ 添加"); btnAddRule->setProperty("type", "primary");
    btnImportRules = new QPushButton("📥 导入 Snort"); btnImportRules->setProperty("type", "primary");
    btnDeleteRule = new QPushButton("🗑 删除"); btnDeleteRule->setProperty("type", "outline-danger");
    btnClearRules = new QPushButton("清空引擎"); btnClearRules->setProperty("type", "danger");

    connect(btnAddRule, &QPushButton::clicked, this, &IdsRulesTab::onAddRule);
    connect(btnImportRules, &QPushButton::clicked, this, &IdsRulesTab::onImportRules);
    connect(btnDeleteRule, &QPushButton::clicked, this, &IdsRulesTab::onDeleteRule);
    connect(btnClearRules, &QPushButton::clicked, this, &IdsRulesTab::onClearRulesClicked);

    toolbar->addWidget(ruleNameInput, 2);
    toolbar->addWidget(rulePatternInput, 3);
    toolbar->addWidget(btnAddRule);
    toolbar->addWidget(btnImportRules);
    toolbar->addWidget(btnDeleteRule);
    toolbar->addWidget(btnClearRules);
    rightDataLayout->addLayout(toolbar);

    idsTable = new QTableWidget(0, 4);
    idsTable->setHorizontalHeaderLabels({"ID", "级别", "检测特征", "详细描述"});
    idsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    idsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    idsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    idsTable->verticalHeader()->setVisible(false);
    idsTable->setShowGrid(false);
    idsTable->hideColumn(0);
    idsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    idsTable->setColumnWidth(1, 80);
    idsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    idsTable->setColumnWidth(2, 300);
    idsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    rightDataLayout->addWidget(idsTable);
    
    bottomHSplitter->addWidget(rightDataContainer);
    bottomHSplitter->setStretchFactor(0, 3);
    bottomHSplitter->setStretchFactor(1, 7);
    contentVSplitter->addWidget(bottomHSplitter);
    contentVSplitter->setStretchFactor(0, 3);
    contentVSplitter->setStretchFactor(1, 7);

    mainLayout->addWidget(contentVSplitter);
}

void IdsRulesTab::loadPersistedData() const {
    auto rules = DatabaseManager::instance().loadRules();
    idsTable->setRowCount(0);
    for (const auto& r : rules) {
        SecurityEngine::instance().addRule(r);
        int row = idsTable->rowCount();
        idsTable->insertRow(row);
        idsTable->setItem(row, 0, new QTableWidgetItem(QString::number(r.id)));
        QString levelStr = r.level == Alert::Critical ? "严重" : (r.level == Alert::High ? "高危" : (r.level == Alert::Medium ? "中危" : "低/信息"));
        idsTable->setItem(row, 1, new QTableWidgetItem(levelStr));
        idsTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(r.pattern)));
        idsTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(r.description)));
    }
    if (!rules.empty()) SecurityEngine::instance().compileRules();
    updateStatCards();
}

void IdsRulesTab::updateStatCards() const {
    int total = idsTable->rowCount();
    int highCount = 0;
    for (int i = 0; i < total; ++i) {
        QString lvl = idsTable->item(i, 1)->text();
        if (lvl == "严重" || lvl == "高危") highCount++;
    }
    cardTotalRules->setValue(QString::number(total));
    cardHighThreats->setValue(QString::number(highCount));
    cardIntercepted->setValue(QString::number(globalSkippedCount));
}

void IdsRulesTab::onFilterTreeClicked(const QTreeWidgetItem *item, int column) {
    if (!item) return;
    Q_UNUSED(column);
    QString filter = item->text(0);
    if (filter.contains("全部") || filter == "📂 策略来源" || filter == "🚨 威胁等级") {
        for (int i = 0; i < idsTable->rowCount(); ++i) idsTable->setRowHidden(i, false);
        return;
    }
    for (int i = 0; i < idsTable->rowCount(); ++i) {
        bool show = false;
        QString levelStr = idsTable->item(i, 1)->text();
        QString descStr = idsTable->item(i, 3)->text();
        if (filter.contains("Critical") && levelStr == "严重") show = true;
        else if (filter.contains("High") && levelStr == "高危") show = true;
        else if (filter.contains("Medium") && levelStr == "中危") show = true;
        else if (filter.contains("Low") && levelStr == "低/信息") show = true;
        else if (filter.contains("本地") && descStr.contains("自定义")) show = true;
        else if (filter.contains("Snort") && descStr.contains("[")) show = true;
        idsTable->setRowHidden(i, !show);
    }
}

void IdsRulesTab::onAddRule() {
    QString name = ruleNameInput->text().trimmed();
    QString pattern = rulePatternInput->text().trimmed();
    if(name.isEmpty() || pattern.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "规则描述和特征码不能为空！");
        return;
    }
    IdsRule rule;
    rule.id = QDateTime::currentSecsSinceEpoch() % 100000;
    rule.enabled = true;
    rule.protocol = "ANY";
    rule.pattern = pattern.toStdString();
    rule.description = "[自定义] " + name.toStdString();
    rule.level = Alert::High;

    DatabaseManager::instance().saveRule(rule);
    SecurityEngine::instance().addRule(rule);
    SecurityEngine::instance().compileRules();

    int row = idsTable->rowCount();
    idsTable->insertRow(row);
    idsTable->setItem(row, 0, new QTableWidgetItem(QString::number(rule.id)));
    idsTable->setItem(row, 1, new QTableWidgetItem("高危"));
    idsTable->setItem(row, 2, new QTableWidgetItem(pattern));
    idsTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(rule.description)));

    ruleNameInput->clear();
    rulePatternInput->clear();
    updateStatCards();
}

void IdsRulesTab::onDeleteRule() {
    if (idsTable->selectedItems().isEmpty()) return;
    int row = idsTable->currentRow();
    int ruleId = idsTable->item(row, 0)->text().toInt();

    DatabaseManager::instance().deleteRule(ruleId);
    idsTable->removeRow(row);

    SecurityEngine::instance().removeRule(ruleId);
    SecurityEngine::instance().compileRules();

    updateStatCards();
    QMessageBox::information(this, "已移除", "规则已从本地库移除，底层检测引擎已无缝热重载。");
}

void IdsRulesTab::onClearRulesClicked() {
    if (QMessageBox::Yes == QMessageBox::question(this, "极度危险", "这将清空数据库中所有的规则并置空引擎！\n确定执行？", QMessageBox::Yes | QMessageBox::No)) {
        DatabaseManager::instance().clearRules();
        idsTable->setRowCount(0);

        SecurityEngine::instance().clearRules();
        SecurityEngine::instance().compileRules();

        updateStatCards();
        QMessageBox::information(this, "完成", "数据库已清空，底层 AC 自动机检测树已完全热卸载。");
    }
}

void IdsRulesTab::onImportRules() {
    QString fileName = QFileDialog::getOpenFileName(this, "导入 Snort 规则", "", "Snort Rules (*.rules);;All Files (*)", nullptr, QFileDialog::DontUseNativeDialog);
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    std::vector<IdsRule> batchRules;
    int addedCount = 0;

    idsTable->setUpdatesEnabled(false);
    idsTable->setSortingEnabled(false);
    
    QRegularExpression reMsg("msg:\"([^\"]+)\"");
    QRegularExpression reContent("content:\"([^\"]+)\"");
    QRegularExpression reClass("classtype:([^;]+);");
    QRegularExpression rePriority("priority:([0-9]+);");
    QRegularExpression reSid("sid:([0-9]+);");
    QRegularExpression reRef("reference:([^;]+);");

    static int importId = 50000;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        auto matchMsg = reMsg.match(line);
        auto matchContent = reContent.match(line);

        if (matchMsg.hasMatch() && matchContent.hasMatch()) {
            QString rawContent = matchContent.captured(1);
            std::string parsedPattern;
            
            bool inHex = false;
            QString hexBuf;
            for (int i = 0; i < rawContent.length(); ++i) {
                if (rawContent[i] == '|') {
                    if (inHex && !hexBuf.isEmpty()) {
                        for (const QString& h : hexBuf.split(' ', Qt::SkipEmptyParts)) {
                            bool ok; char byte = static_cast<char>(h.toInt(&ok, 16));
                            if (ok) parsedPattern.push_back(byte);
                        }
                    }
                    inHex = !inHex;
                } else {
                    if (inHex) hexBuf.append(rawContent[i]);
                    else parsedPattern.push_back(rawContent[i].toLatin1());
                }
            }

            if (inHex && !hexBuf.isEmpty()) {
                for (const QString& h : hexBuf.split(' ', Qt::SkipEmptyParts)) {
                    bool ok; char byte = static_cast<char>(h.toInt(&ok, 16));
                    if (ok) parsedPattern.push_back(byte);
                }
            }

            if (parsedPattern.length() < 6 || parsedPattern == "GET / " || parsedPattern == "POST /") {
                globalSkippedCount++;
                continue;
            }

            Alert::Level dynamicLevel = Alert::Info;

            QString clsStr = reClass.match(line).hasMatch() ? reClass.match(line).captured(1).toLower() : "";
            QString msgStr = matchMsg.captured(1).toLower();

            if (clsStr.contains("successful-admin") || msgStr.contains("ransomware") ||
                msgStr.contains("cobalt strike") || msgStr.contains("reverse shell") ||
                msgStr.contains("apt") || msgStr.contains("meterpreter")) {
                dynamicLevel = Alert::Critical;
            }
            else if (clsStr.contains("trojan-activity") || clsStr.contains("malware-cnc") ||
                     clsStr.contains("successful-user") || msgStr.contains("backdoor") ||
                     msgStr.contains("c2 ") || msgStr.contains("c&c") || msgStr.contains("botnet")) {
                dynamicLevel = Alert::High;
            }
            else if (clsStr.contains("attempted") || clsStr.contains("web-application") ||
                     clsStr.contains("exploit") || clsStr.contains("dos") ||
                     clsStr.contains("suspicious")) {
                dynamicLevel = Alert::Medium;
            }
            else if (clsStr.contains("policy") || clsStr.contains("misc") ||
                     clsStr.contains("recon") || clsStr.contains("scan") ||
                     clsStr.contains("leak") || clsStr.contains("bad-unknown") ||
                     clsStr.contains("network-scan")) {
                dynamicLevel = Alert::Low;
            }
            else {
                if (rePriority.match(line).hasMatch()) {
                    int prio = rePriority.match(line).captured(1).toInt();
                    if (prio == 1) dynamicLevel = Alert::Medium;
                    else if (prio == 2) dynamicLevel = Alert::Low;
                    else dynamicLevel = Alert::Info;
                }
            }

            QString sidStr = reSid.match(line).hasMatch() ? reSid.match(line).captured(1) : "N/A";
            QString refStr = reRef.match(line).hasMatch() ? reRef.match(line).captured(1).replace(",", ": ") : "None";
            QString desc = QString("[%1] %2 | 分类: %3").arg(sidStr, matchMsg.captured(1), clsStr.isEmpty() ? "Unknown" : clsStr);

            IdsRule rule;
            rule.id = ++importId;
            rule.enabled = true;
            rule.protocol = "ANY";
            rule.pattern = parsedPattern;
            rule.description = desc.toStdString();
            rule.level = dynamicLevel;

            batchRules.push_back(rule);
            addedCount++;

            if (addedCount % 200 == 0) {
                QCoreApplication::processEvents();
            }
        }
    }
    file.close();

    if (addedCount > 0) {
        DatabaseManager::instance().saveRulesTransaction(batchRules);
        for (const auto& r : batchRules) {
            SecurityEngine::instance().addRule(r);
            int row = idsTable->rowCount();
            idsTable->insertRow(row);
            idsTable->setItem(row, 0, new QTableWidgetItem(QString::number(r.id)));

            QString lvl = r.level == Alert::Critical ? "严重" : (r.level == Alert::High ? "高危" : (r.level == Alert::Medium ? "中危" : "低/信息"));
            idsTable->setItem(row, 1, new QTableWidgetItem(lvl));
            idsTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(r.pattern)));
            idsTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(r.description)));
        }
        SecurityEngine::instance().compileRules();
        updateStatCards();
        QMessageBox::information(this, "智能清洗导入成功", QString("基于 SOC 字典的定级清洗完毕！\n写入规则: %1 条\n已剔除无效泛化特征: %2 条").arg(addedCount).arg(globalSkippedCount));
    } else {
        QMessageBox::warning(this, "导入失败", "未提取到有效规则。");
    }

    idsTable->setUpdatesEnabled(true);
    idsTable->setSortingEnabled(true);
}