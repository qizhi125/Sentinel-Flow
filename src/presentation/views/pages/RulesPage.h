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

public slots:
    void onThemeChanged();

private slots:
    void onTabClicked(int id);
    void onApplyBpf();
    void onAddBlacklist();
    void onRemoveBlacklist();
    void onBrowseLogPath();

private:
    void setupUi();
    void loadPersistedData();

    QWidget* createBpfPage();
    QWidget* createBlacklistPage();
    QWidget* createProtocolPage();
    QWidget* createAuditPage();

    QButtonGroup *tabGroup;
    QStackedWidget *contentStack;

    QTextEdit *bpfEditor;
    QComboBox *bpfPresets;
    QPushButton *btnApplyBpf;

    QTableWidget *blacklistTable;
    QLineEdit *blockIpInput;
    QPushButton *btnAddBlock;
    QPushButton *btnRemoveBlock;

    QLineEdit *logPathInput;
    QPushButton *btnBrowse;
};