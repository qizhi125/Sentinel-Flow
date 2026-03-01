#pragma once
#include "common/types/NetworkTypes.h"
#include "presentation/views/components/TrafficTableModel.h"
#include <QWidget>
#include <QTableView>
#include <QTextEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <deque>

class TrafficProxyModel;

class TrafficMonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit TrafficMonitorPage(QWidget *parent = nullptr);
    ~TrafficMonitorPage() override = default;

    void addPacket(const ParsedPacket& packet);

public slots:
    void onThemeChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void processPendingPackets();
    void onExportClicked();
    void onFilterChanged();
    void onHexViewContextMenu(const QPoint& pos);

private:
    void setupUi();
    void updateHexView(int row);
    void refreshTable();

    QTableView *tableView;
    TrafficTableModel *tableModel;
    TrafficProxyModel *proxyModel;

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
    QTimer *filterDebounceTimer;

    std::deque<ParsedPacket> pendingPackets;
    bool isPaused = false;
    bool hoverPaused = false;

    int lastRowRepeatCount = 1;
};