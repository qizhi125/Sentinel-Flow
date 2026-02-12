#pragma once
#include "common/types/NetworkTypes.h"
#include <QWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <vector>
#include <deque>

class TrafficMonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit TrafficMonitorPage(QWidget *parent = nullptr);
    void addPacket(const ParsedPacket& packet);

private slots:
    void processPendingPackets();
    void onExportClicked();
    void onFilterChanged(); // 🔥

private:
    void setupUi();
    void updateHexView(int row);
    void refreshTable();
    QTreeWidgetItem* addTreeItem(QTreeWidgetItem *parent, const QString &title, const QString &value = "");

    QTableWidget *table;
    QLabel *lblSelectedInfo;
    QTextEdit *hexView;
    QTreeWidget *protoTree;

    QLineEdit *searchBox;
    QComboBox *comboProtocol;
    QCheckBox *chkAutoScroll;
    QCheckBox *chkCollapse;
    QPushButton *btnPause;
    QPushButton *btnExport;
    QPushButton *btnClear;

    QTimer *uiTimer;
    std::deque<ParsedPacket> pendingPackets;
    std::vector<ParsedPacket> packetBuffer;
    bool isPaused = false;

    const size_t MAX_BUFFER_SIZE = 10000;
    const int MAX_UI_ROWS = 500;
    int m_localSeq = 0;
    int lastRowRepeatCount = 1;
};