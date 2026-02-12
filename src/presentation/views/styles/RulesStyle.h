#pragma once
#include "global.h"
#include "ThemeDefinitions.h"

namespace RulesStyle {
    inline QString getTabButton() { return g_isDarkMode ? ThemeDef::Dark::TabButton : ThemeDef::Light::TabButton; }

    // 信息框样式 - 浅色模式
    inline QString getInfoBoxLight() {
        return "QFrame { background-color: rgba(0, 191, 165, 0.08); border-left: 4px solid #009688; border-radius: 4px; padding: 15px; }";
    }

    // 信息框样式 - 深色模式
    inline QString getInfoBoxDark() {
        return "QFrame { background-color: rgba(0, 191, 165, 0.15); border-left: 4px solid #00BFA5; border-radius: 4px; padding: 15px; }";
    }

    // 信息框标题样式 - 浅色模式
    inline QString getInfoTitleLight() {
        return "QLabel#InfoTitle { font-weight: bold; font-size: 14px; margin-bottom: 5px; color: #000000 !important; background: transparent; }";
    }

    // 信息框标题样式 - 深色模式
    inline QString getInfoTitleDark() {
        return "QLabel#InfoTitle { font-weight: bold; font-size: 14px; margin-bottom: 5px; color: #FFFFFF !important; background: transparent; }";
    }

    // 信息框内容样式 - 浅色模式
    inline QString getInfoContentLight() {
        return "QLabel#InfoContent { font-size: 14px; color: #000000 !important; background: transparent; font-weight: 500; }";
    }

    // 信息框内容样式 - 深色模式
    inline QString getInfoContentDark() {
        return "QLabel#InfoContent { font-size: 14px; color: #E0E0E0 !important; background: transparent; font-weight: 500; }";
    }

    inline QString getInfoBox() {
        QString bgColor = g_isDarkMode ? "rgba(0, 191, 165, 0.15)" : "rgba(0, 191, 165, 0.08)";
        QString borderColor = g_isDarkMode ? "#00BFA5" : "#009688";
        return QString("QFrame { background-color: %1; border-left: 4px solid %2; border-radius: 4px; padding: 15px; }").arg(bgColor, borderColor);
    }

    inline QString getInfoTitle() {
        QString textColor = g_isDarkMode ? "#E0E0E0" : "#000000";
        return QString("QLabel#InfoTitle { font-weight: bold; font-size: 14px; margin-bottom: 5px; color: %1 !important; background: transparent; }").arg(textColor);
    }

    inline QString getInfoContent() {
        QString textColor = g_isDarkMode ? "#CCCCCC" : "#000000";
        return QString("QLabel#InfoContent { font-size: 14px; color: %1 !important; background: transparent; font-weight: 500; }").arg(textColor);
    }

    inline const QString Input = "QLineEdit { background-color: palette(base); border: 1px solid palette(mid); padding: 5px; border-radius: 4px; color: palette(text); }";
    inline const QString Editor = "QTextEdit { background-color: palette(base); border: 1px solid palette(mid); border-radius: 4px; color: palette(text); }";

    inline QString getGroupBox() {
        QString border = g_isDarkMode ? "#444" : "#888";
        QString color  = "#009688";
        return QString(
            "QGroupBox { border: 1px solid %1; border-radius: 6px; margin-top: 10px; padding-top: 15px; font-weight: bold; color: %2; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; }"
        ).arg(border, color);
    }

    inline QString getCheckBox() {
        QString textColor = g_isDarkMode ? "#E0E0E0" : "#000000";
        QString borderColor = g_isDarkMode ? "#666" : "#555";

        return QString(
            "QCheckBox { font-size: 14px; spacing: 8px; background: transparent; color: %1 !important; font-weight: 600; }"
            "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %2; border-radius: 4px; background: transparent; }"
            "QCheckBox::indicator:unchecked:hover { border: 1px solid #00BFA5; }"
            "QCheckBox::indicator:checked { border: 1px solid #00BFA5; background: transparent; image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld0JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9IiMwMEJGQTUiIHN0cm9rZS13aWR0aD0iMyIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=); }"
        ).arg(textColor, borderColor);
    }

    inline QString getBtnAction() { return g_isDarkMode ? ThemeDef::Dark::BtnNormal : ThemeDef::Light::BtnNormal; }
    inline QString getBtnDangerOutline() { return g_isDarkMode ? ThemeDef::Dark::BtnOutline : ThemeDef::Light::BtnOutline; }
    inline const QString BtnDangerFilled = "QPushButton { background-color: #FF4444; color: #FFFFFF; border-radius: 4px; padding: 6px 12px; font-weight: bold; border: none; } QPushButton:hover { background-color: #CC3333; }";
}