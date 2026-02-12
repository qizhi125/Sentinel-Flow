#pragma once
#include "global.h"

namespace SettingsStyle {
    inline const QString SectionTitle = "QLabel { color: #009688; font-size: 18px; font-weight: bold; margin: 15px 0; background: transparent; }";
    inline const QString Separator = "border: none; border-top: 1px solid rgba(128,128,128,0.2); margin: 25px 0;";

    inline const QString ScrollArea    = "QScrollArea { border: none; background: transparent; }";
    inline const QString ContentWidget = "QWidget { background: transparent; }";
    inline const QString Input         = "QLineEdit { background-color: palette(base); border: 1px solid palette(mid); border-radius: 4px; padding: 8px; color: palette(text); font-size: 15px; }";
    inline const QString BtnDanger     = "QPushButton { background-color: #FF3333; color: #FFFFFF; border: 1px solid #CC0000; border-radius: 4px; padding: 8px 20px; font-weight: bold; font-size: 15px; }";

    inline QString getLabel() {
        return QString("font-size: 16px; font-weight: 600; background: transparent; color: palette(text);");
    }

    inline QString getRadioButton() {
        QString border = g_isDarkMode ? "#666" : "#555";
        return QString(
            "QRadioButton { font-size: 15px; spacing: 10px; font-weight: 600; background: transparent; color: palette(text); }"
            "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 9px; border: 1px solid %1; background: transparent; }"
            "QRadioButton::indicator:checked { border: 1px solid #00BFA5; background-color: transparent; image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMCIgaGVpZ2h0PSIxMCIgdmlld0JveD0iMCAwIDEwIDEwIj48Y2lyY2xlIGN4PSI1IiBjeT0iNSIgcj0iNCIgZmlsbD0iIzAwQkZBNSIvPjwvc3ZnPg==); }"
        ).arg(border);
    }

    inline QString getCheckBox() {
        QString border = g_isDarkMode ? "#666" : "#555";
        return QString(
            "QCheckBox { font-size: 15px; spacing: 10px; font-weight: 600; background: transparent; color: palette(text); }"
            "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %1; border-radius: 4px; background: transparent; }"
            "QCheckBox::indicator:checked { border: 1px solid #00BFA5; image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld0JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9IiMwMEJGQTUiIHN0cm9rZS13aWR0aD0iMyIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=); }"
        ).arg(border);
    }

    const QString BtnPrimary = "QPushButton { background-color: #00BFA5; color: #FFFFFF; border-radius: 4px; padding: 10px 20px; font-weight: bold; font-size: 15px; } QPushButton:hover { background-color: #00E5C5; }";
}