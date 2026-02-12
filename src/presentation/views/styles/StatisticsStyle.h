#pragma once
#include "global.h"

namespace StatisticsStyle {
    inline const QString ColorRate      = Style::ColorAccentDark;
    inline const QString ColorTotal     = "#3498DB";
    inline const QString ColorPPS       = "#9B59B6";
    inline const QString ColorThreat    = "#FF4444";

    inline const QString ColorDefault   = "#888888";
    inline const QString ColorTCP       = "#5D5FEF";
    inline const QString ColorUDP       = "#00BFA5";
    inline const QString ColorTLS       = "#E74C3C";
    inline const QString ColorHTTP      = "#F1C40F";
    inline const QString ColorDDoS      = "#E74C3C";
    inline const QString ColorMalware   = "#9B59B6";
    inline const QString ColorPhishing  = "#F1C40F";

    inline const QString SectionTitle = "QLabel { color: #009688; font-size: 18px; font-weight: bold; background: transparent; }";

    inline const QString MetricTitle = "font-size: 14px; font-weight: bold; color: palette(text); opacity: 0.8; background: transparent;";
    inline const QString LegendBox   = "border-radius: 2px; background-color: %1;";
    inline const QString ScrollArea  = "QScrollArea { border: none; background: transparent; }";

    inline const QString HostBar = "QProgressBar { background: palette(midlight); border: none; border-radius: 4px; } QProgressBar::chunk { background: " + Style::ColorAccentDark + "; border-radius: 4px; }";

    inline QString getMetricCardStyle(const QString& color) {
        return QString("#Card { border-left: 5px solid %1; background: palette(base); border-radius: 4px; }").arg(color);
    }

    inline QString getMetricValue() {
        return QString("QLabel { color: palette(text); font-size: 24px; font-weight: 800; font-family: 'JetBrains Mono', monospace; background: transparent; }");
    }

    inline QString getLegendLabel() {
        return QString("QLabel { color: palette(text); font-size: 14px; padding-left: 5px; background: transparent; font-weight: 500; }");
    }

    inline QString getProtoLabel() {
        return QString("font-family: 'JetBrains Mono'; font-size: 14px; color: palette(text); background: transparent; font-weight: bold;");
    }

    inline QString getProtoPercent() {
        return QString("font-family: 'JetBrains Mono'; font-size: 14px; color: palette(text); background: transparent; font-weight: 800;");
    }

    inline QString getProtoBarStyle(const QString& color) {
        return QString("QProgressBar { background: palette(midlight); border: none; border-radius: 4px; } QProgressBar::chunk { background: %1; border-radius: 4px; }").arg(color);
    }

    inline QString getEmptyLabel() {
        QString col = g_isDarkMode ? "#888888" : "#333333";
        return QString("color: %1; font-style: italic; background: transparent; font-size: 14px;").arg(col);
    }

    inline QString getHostIp() {
        return QString("font-family: 'JetBrains Mono'; font-size: 14px; color: palette(text); background: transparent; font-weight: 500;");
    }
    
    inline QString getHostNum() {
        return QString("font-family: 'JetBrains Mono'; font-size: 14px; color: palette(text); font-weight: 800; background: transparent;");
    }

    inline QString formatSpeedHtml(const QString& rx, const QString& tx) {
        return QString("<span style='color:%1;'>⬇ %2</span> <span style='color:%3; margin-left:12px;'>⬆ %4</span>").arg(ColorRate, rx, ColorPPS, tx);
    }
    inline QString formatTotalHtml(const QString& rx, const QString& tx) {
        return QString("<span style='color:palette(text); font-weight:800;'>%1 / %2</span>").arg(rx, tx);
    }
}