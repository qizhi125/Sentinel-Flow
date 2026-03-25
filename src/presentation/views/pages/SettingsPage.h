#pragma once
#include "ThemeablePage.h"
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>

class SettingsPage : public ThemeablePage {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void onThemeChanged() override;

    signals:
        void captureInterfaceChanged(const QString& iface);
        void themeChanged(bool isDark);

private slots:
    void onApplyEngineSettings();
    void onThemeSelectionChanged(int index);
    void onVacuumDatabase();

private:
    void setupUi();
    void loadPersistedSettings();
    QWidget* createSectionCard(const QString& title);

    QComboBox *cbInterface;
    QCheckBox *chkPromiscuous;
    QSpinBox *spinBufferSize;
    QPushButton *btnApplyEngine;

    QComboBox *cbTheme;
    QCheckBox *chkAutoStart;

    QComboBox *cbLogRetention;
    QPushButton *btnVacuumDb;
};
