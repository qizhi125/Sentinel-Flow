#pragma once
#include "global.h"

namespace TrafficStyle {

    inline const QString ColorTCP  = "#5D5FEF";
    inline const QString ColorUDP  = "#00BFA5";
    inline const QString ColorHTTP = "#E67E22";
    inline const QString ColorTLS  = "#9B59B6";
    inline const QString ColorOther= "#888888";

    inline QString getHexViewStyle() {
        if (g_isDarkMode) {
            return "QTextEdit { font-family: 'JetBrains Mono'; font-size: 14px; color: #E6DB74; background-color: #1E1E1E; border: none; }";
        } else {
            return "QTextEdit { font-family: 'JetBrains Mono'; font-size: 14px; color: #111111; background-color: #FFFFFF; border: 1px solid #888; }";
        }
    }

    inline QString getSearchBox() {
        QString bg = g_isDarkMode ? "rgba(255,255,255,0.06)" : "#FFFFFF";
        QString bd = g_isDarkMode ? "rgba(255,255,255,0.12)" : "#888888";
        QString col = g_isDarkMode ? "#E0E0E0" : "#111111";
        return QString(
            "QLineEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 6px 10px; color: %3; }"
            "QLineEdit:focus { border: 1px solid %4; }"
        ).arg(bg, bd, col, Style::ColorAccentDark);
    }

    inline QString getComboBox() {
        QString bg = g_isDarkMode ? "rgba(255,255,255,0.06)" : "#FFFFFF";
        QString bd = g_isDarkMode ? "rgba(255,255,255,0.12)" : "#888888";
        QString col = g_isDarkMode ? "#E0E0E0" : "#111111";
        QString arrow = g_isDarkMode ? "rgba(128,128,128,0.8)" : "#111111";
        return QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 10px; color: %3; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid %4; margin-right: 8px; }"
        ).arg(bg, bd, col, arrow);
    }

    // 复选框：浅色模式纯黑加粗
    inline QString getCheckBox() {
        QString col = g_isDarkMode ? "#E0E0E0" : "#000000";
        QString bd = g_isDarkMode ? "#666" : "#555";
        return QString(
            "QCheckBox { font-size: 15px; spacing: 8px; background: transparent; color: %1; font-weight: bold; }"
            "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %2; border-radius: 4px; background: transparent; }"
            "QCheckBox::indicator:unchecked:hover { border: 1px solid #00BFA5; }"
            "QCheckBox::indicator:checked { border: 1px solid #00BFA5; background: transparent; image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld0JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9IiMwMEJGQTUiIHN0cm9rZS13aWR0aD0iMyIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=); }"
        ).arg(col, bd);
    }

    // 导出按钮：浅色模式纯黑加粗
    inline QString getBtnNormal() {
        if (!g_isDarkMode) {
            return "QPushButton { background: #FFFFFF; border: 1px solid #888; color: #111111; border-radius: 6px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #F0F0F0; }";
        }
        return ThemeDef::Dark::BtnNormal;
    }

    inline QString getBtnPaused() {
        if (!g_isDarkMode) {
            return "QPushButton { background-color: #FFC107; color: #111111; font-weight: 800; border-radius: 6px; padding: 6px 12px; border: none; } QPushButton:hover { background-color: #FFB300; }";
        }
        return "QPushButton { background-color: #F1C40F; color: #111111; font-weight: bold; border-radius: 6px; padding: 6px 12px; border: none; } QPushButton:hover { background-color: #D4AC0D; }";
    }

    inline QString getBtnDanger() {
        return "QPushButton { background-color: #FF3333; color: #FFFFFF; border: 1px solid #CC0000; border-radius: 4px; padding: 8px 20px; font-weight: 800; font-size: 14px; } QPushButton:hover { background-color: #FF6666; }";
    }

    inline const QString SelectedInfo = "QLabel { color: #009688; font-size: 15px; font-weight: bold; padding: 8px 4px; background: transparent; }";

    inline const QString ProtoTree = "QTreeWidget { background: transparent; border: 1px solid palette(mid); color: palette(text); font-size: 14px; }";
}