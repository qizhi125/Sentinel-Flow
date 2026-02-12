#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QButtonGroup>
#include <QCheckBox>

class RulesPage : public QWidget {
    Q_OBJECT
public:
    explicit RulesPage(QWidget *parent = nullptr);

private slots:
    void onTabClicked(int id);
    void onApplyBpf(); // BPF 应用
    void onAddRule();  // IDS 添加
    void onDeleteRule(); // IDS 删除
    void onImportRules();   // 导入 Snort 规则
    void onAddBlacklist(); // 黑名单添加
    void onRemoveBlacklist(); // 黑名单删除
    void onBrowseLogPath(); // 审计路径选择

private:
    void setupUi();

    // 二级界面创建函数
    QWidget* createBpfPage();
    QWidget* createIdsPage();
    QWidget* createBlacklistPage();
    QWidget* createProtocolPage();
    QWidget* createAuditPage();

    QWidget* createInfoBox(const QString& title, const QString& text);

    QButtonGroup *tabGroup;
    QStackedWidget *contentStack;

    // BPF Controls
    QTextEdit *bpfEditor;
    QComboBox *bpfPresets;
    QPushButton *btnApplyBpf;

    // IDS Controls
    QTableWidget *idsTable;
    QLineEdit *ruleNameInput;
    QPushButton *btnAddRule;
    QPushButton *btnDeleteRule;
    QPushButton *btnImportRules;

    // Blacklist Controls
    QTableWidget *blacklistTable;
    QLineEdit *blockIpInput;
    QPushButton *btnAddBlock;
    QPushButton *btnRemoveBlock;

    // Audit Controls
    QLineEdit *logPathInput;
};