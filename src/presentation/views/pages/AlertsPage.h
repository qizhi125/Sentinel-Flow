#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <vector>
#include "common/types/NetworkTypes.h"

class AlertsPage : public QWidget {
    Q_OBJECT

public:
    explicit AlertsPage(QWidget *parent = nullptr);
    void addAlert(const Alert& alert);

private slots:
    void onExportClicked();
    void onFilterChanged();
    void onClearClicked();

private:
    void setupUi();
    void updateDetailView(const Alert& alert);
    void refreshTable(); // 根据过滤条件刷新表格

    // UI Controls
    QLineEdit *searchBox;
    QComboBox *levelFilter;
    QPushButton *btnExport;
    QPushButton *btnClear;

    QTableWidget *alertTable;
    QLabel *lblDetailTitle;
    QTextEdit *txtDetailContent;

    std::vector<Alert> alertsBuffer;
};