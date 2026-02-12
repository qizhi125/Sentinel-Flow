#pragma once
#include <QString>
#include "global.h"
#include "ThemeDefinitions.h"

namespace DashboardStyle {

    const QString ColorSafe    = Style::ColorAccentDark;
    const QString ColorWarning = Style::ColorWarning;
    const QString ColorDanger  = Style::ColorDanger;
    const QString ColorInfo    = Style::ColorInfo;
    const QString ColorNeutral = Style::ColorNeutral;

    inline QString getResourceTitle() {
        return "font-size: 16px; color: palette(windowText); margin-bottom: 4px; background: transparent; font-weight: bold;";
    }

    inline QString getResourceValue() {
        return "font-size: 14px; color: palette(text); font-family: 'Consolas'; background: transparent; font-weight: bold;";
    }

    inline QString getSectionTitle() {
        QString color = "#009688";
        return QString("QLabel { color: %1; font-size: 18px; font-weight: bold; margin-bottom: 12px; border-left: 5px solid %1; padding-left: 12px; border-bottom: 1px solid palette(mid); padding-bottom: 8px; background: transparent; }").arg(color);
    }

    inline QString getCardStyle(const QString& color) {
        return QString("#Card { border-left: 6px solid %1; background: palette(base); border-radius: 4px; }").arg(color);
    }

    inline QString getStatusTitle() {
        return "font-size: 14px; font-weight: bold; text-transform: uppercase; background: transparent; color: palette(text);";
    }

    inline QString getStatusValue() {
        return "font-size: 32px; font-weight: 800; font-family: 'JetBrains Mono'; background: transparent; color: palette(text);";
    }

    inline QString getProgressBarStyle(const QString& color) {
        return QString(
            "QProgressBar { background: palette(midlight); border: none; border-radius: 4px; }"
            "QProgressBar::chunk { background: %1; border-radius: 4px; }"
        ).arg(color);
    }

    inline QString getServiceLabel() {
        return "font-size: 15px; font-weight: bold; color: palette(text); background: transparent;";
    }

    const QString ServiceStatusOK = "color: " + ColorSafe + "; font-size: 14px; font-weight: bold; background: transparent;";
    const QString ServiceStatusERR = "color: " + ColorDanger + "; font-size: 14px; font-weight: bold; background: transparent;";

    inline QString getActivityList() {
        return QString(
            "QListWidget { background-color: transparent; border: none; outline: none; }"
            "QListWidget::item { padding: 10px 5px; border-bottom: 1px dashed palette(mid); font-family: 'JetBrains Mono', monospace; font-size: 14px; color: palette(text); font-weight: 600; }"
        );
    }
}