#pragma once
#include "presentation/views/components/TrafficTableModel.h"
#include "common/types/NetworkTypes.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QTextEdit>
#include <QTreeWidget>
#include <QProgressBar>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QThread>
#include <vector>

#include "ThemeablePage.h"

class ForensicProxyModel;
class PcapLoaderThread;

class ForensicPage : public ThemeablePage {
    Q_OBJECT
public:
    explicit ForensicPage(QWidget *parent = nullptr);
    void onThemeChanged() override;
    ~ForensicPage() override;

private slots:
    void onLoadPcapClicked();
    void onClearClicked();
    void onFilterChanged();
    void handleBatchReady(const QVector<ParsedPacket>& batch);
    void handleProgress(int percent);
    void handleLoaderFinished(const QString& msg);
    void handleLoaderError(const QString& err);

private:
    void setupUi();
    void updateHexView(int row);

    QTableView *tableView;
    TrafficTableModel *tableModel;
    ForensicProxyModel *proxyModel;

    QLabel *lblStatus;
    QPushButton *btnLoadFile;
    QPushButton *btnClear;
    QProgressBar *progressBar;
    QLineEdit *searchBox;
    QComboBox *comboProtocol;

    QLabel *lblSelectedInfo;
    QTextEdit *hexView;
    QTreeWidget *protoTree;

    PcapLoaderThread *loaderThread = nullptr;
    QString currentPcapPath;

    std::vector<ParsedPacket> tempBuffer;
};