#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <deque>
#include "common/types/NetworkTypes.h"

class AlertsPage : public QWidget {
    Q_OBJECT

public:
    explicit AlertsPage(QWidget *parent = nullptr);

public slots:
    void addAlert(const Alert& alert);
    void onThemeChanged();

private slots:
    void onExportClicked();
    void onFilterChanged();
    void onClearClicked();
    void onOpenPcapDirClicked();

private:
    void setupUi();
    void updateDetailView(const Alert& alert);
    void refreshTable();

    QLineEdit *searchBox;
    QComboBox *levelFilter;
    QPushButton *btnExport;
    QPushButton *btnClear;

    QTableWidget *alertTable;
    QLabel *lblDetailTitle;
    QTextEdit *txtDetailContent;
    QPushButton *btnOpenPcapDir;
    QTimer* uiRefreshTimer;

    bool isDirty = false;
    std::deque<Alert> alertsBuffer;
};