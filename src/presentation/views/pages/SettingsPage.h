#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    signals:
        void themeChanged(bool isDark);
    void captureInterfaceChanged(const QString& iface);
    void refreshRateChanged(int ms);
    void dataRetentionChanged(int days);

private slots:
    void onApplyCoreClicked();
    void onClearDataClicked();
    void onRetentionToggled(int id); // 处理单选按钮点击

private:
    void setupUi();

    // 辅助函数
    void populateInterfaces();
    void addSeparator(QVBoxLayout* layout); // 添加分割线
    void addSectionTitle(QVBoxLayout* layout, const QString& title);

    // --- UI Components ---

    // Core Engine
    QComboBox *comboInterface;
    QCheckBox *chkPromiscuous;
    QLineEdit *editGlobalBpf;
    QPushButton *btnRestartEngine;

    // Display
    QSpinBox *spinRefreshRate;
    QSpinBox *spinHistoryPoints;
    QComboBox *comboTheme;

    // Storage
    QButtonGroup *grpRetention;
    QLineEdit *editSavePath;
    QPushButton *btnClearDb;
};