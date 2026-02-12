#pragma once
#include "global.h"
#include <QColor>

namespace AlertsStyle {

    inline const QString DetailTitle = "QLabel { color: #009688; font-size: 18px; font-weight: bold; padding: 8px 0; background: transparent; }";

    inline const QString PayloadBox = R"(
        QTextEdit { 
            background-color: palette(base); 
            color: palette(text); 
            font-family: 'JetBrains Mono'; 
            font-size: 14px; 
            border: 1px solid palette(mid); 
            padding: 12px; 
        }
    )";

    inline QString getSearchBox() {
        QString bg = g_isDarkMode ? "rgba(255,255,255,0.06)" : "#FFFFFF";
        QString bd = g_isDarkMode ? "rgba(255,255,255,0.12)" : "#888888";
        return QString(
            "QLineEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: palette(text); font-size: 15px; }"
            "QLineEdit:focus { border: 1px solid %3; }"
        ).arg(bg, bd, Style::ColorAccentDark);
    }

    inline QString getComboBox() {
        QString bg = g_isDarkMode ? "rgba(255,255,255,0.06)" : "#FFFFFF";
        QString bd = g_isDarkMode ? "rgba(255,255,255,0.12)" : "#888888";
        QString arrow = g_isDarkMode ? "rgba(128,128,128,0.8)" : "#111111";
        return QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 6px 12px; color: palette(text); font-size: 15px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid %3; margin-right: 10px; }"
        ).arg(bg, bd, arrow);
    }

    inline QString getBtnNormal() { return g_isDarkMode ? ThemeDef::Dark::BtnNormal : ThemeDef::Light::BtnNormal; }

    const QString BtnDanger  = "QPushButton { background-color: #FF3333; color: #FFFFFF; border: 1px solid #CC0000; border-radius: 4px; padding: 8px 20px; font-weight: bold; font-size: 15px; } QPushButton:hover { background-color: #FF6666; }";

    inline QColor getLevelColor(int level) {
        switch(level) {
            case 0: return QColor(Style::ColorDanger);
            case 1: return QColor(Style::ColorWarning);
            case 2: return QColor(Style::ColorAccentDark);
            case 3: return QColor(Style::ColorNeutral);
            default: return QColor("#888888");
        }
    }

    inline QString getDetailHtml(const QString& timeStr, const QString& sourceIp, const QString& desc) {
        const QString t = timeStr.toHtmlEscaped();
        const QString ip = sourceIp.toHtmlEscaped();
        const QString d = desc.toHtmlEscaped();

        QString textColor = g_isDarkMode ? "#E0E0E0" : "#111111";
        QString boxBg     = g_isDarkMode ? "#111" : "#F8F9FA";
        QString boxBorder = g_isDarkMode ? "#333" : "#888";
        QString boxText   = g_isDarkMode ? "#E6DB74" : "#111111";

        return QString(R"(
            <div style="font-family: Inter, sans-serif; font-size: 14px; color: %1;">
              <div style="margin-bottom: 15px;">
                <div><span style="color:%2; font-weight:bold;">Time</span>: <b>%3</b></div>
                <div><span style="color:%2; font-weight:bold;">Source</span>: <b>%4</b></div>
              </div>
              <div style="color:%2; font-weight:bold; margin-bottom:8px;">Description</div>
              <pre style="white-space:pre-wrap; margin:0; padding:15px; background:%5; border:1px solid %6; border-radius:6px; color:%7; font-family:'JetBrains Mono'; font-weight:500;">%8</pre>
            </div>
        )").arg(textColor, Style::ColorAccentDark, t, ip, boxBg, boxBorder, boxText, d);
    }
}