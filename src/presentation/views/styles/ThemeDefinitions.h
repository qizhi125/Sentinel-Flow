#pragma once
#include <QString>

namespace ThemeDef {

    const QString Accent      = "#00BFA5";
    const QString AccentLight = "#009688";
    const QString Danger      = "#FF3333";
    const QString Warning     = "#F1C40F";

    namespace Dark {
        const QString Global = R"(
            QWidget {
                background-color: #1E1E1E;
                color: #E0E0E0;
                font-family: 'Inter', 'Microsoft YaHei';
                font-size: 14px;
            }
            QWidget#Sidebar {
                background-color: #252526;
                border-right: 1px solid #333333;
            }
            QPushButton#SidebarBtn {
                text-align: left;
                padding: 12px 24px;
                border: none;
                border-radius: 6px;
                margin: 4px 8px;
                color: #9DA5B4;
                font-size: 18px;
                font-weight: 500;
                background-color: transparent;
            }
            QPushButton#SidebarBtn:hover {
                background-color: rgba(255, 255, 255, 0.05);
                color: #FFFFFF;
            }
            QPushButton#SidebarBtn:checked {
                background-color: rgba(0, 191, 165, 0.15);
                color: #00BFA5;
                font-weight: bold;
                border-left: 4px solid #00BFA5;
            }
            QLabel { background: transparent; color: #E0E0E0; }
            QLineEdit, QTextEdit, QPlainTextEdit {
                background-color: rgba(255,255,255,0.06);
                border: 1px solid rgba(255,255,255,0.1);
                border-radius: 4px;
                padding: 6px;
                color: #E0E0E0;
            }
            QComboBox {
                background-color: rgba(255,255,255,0.06);
                border: 1px solid rgba(255,255,255,0.1);
                padding: 5px 10px; color: #E0E0E0;
            }
            QSplitter::handle { background: #444444; }
            QTableWidget {
                background-color: #1E1E1E;
                border: 1px solid #333;
                color: #E0E0E0;
                gridline-color: #2D2D2D;
                selection-background-color: rgba(0, 191, 165, 0.15);
            }
            QHeaderView::section {
                background-color: #252526;
                color: #AAAAAA;
                border: none;
                border-bottom: 2px solid #333;
                padding: 6px;
                font-weight: bold;
            }
            QTableWidget::item:selected {
                background-color: rgba(0, 191, 165, 0.15);
                color: #00BFA5;
            }
        )";

        const QString TabButton   = "QPushButton { background: transparent; color: #999; border: none; padding: 8px 25px; font-size: 14px; } QPushButton:checked { color: #00BFA5; border-bottom: 3px solid #00BFA5; font-weight: bold; }";
        const QString InfoBox     = "QFrame { background-color: rgba(0, 191, 165, 0.08); border-left: 4px solid #00BFA5; border-radius: 4px; padding: 15px; }";
        const QString InfoTitle   = "font-weight: bold; font-size: 14px; margin-bottom: 5px; color: #E0E0E0; background: transparent;";
        const QString InfoContent = "font-size: 13px; color: #CCCCCC; background: transparent;";
        const QString BtnOutline  = "QPushButton { background: transparent; border: 1px solid #FF4444; color: #FF4444; border-radius: 4px; padding: 6px 12px; }";
        const QString ResourceTitle = "font-size: 15px; color: #FFFFFF; margin-bottom: 4px; background: transparent;";
        const QString ResourceValue = "font-size: 13px; color: #B0B0B0; opacity: 0.9; font-family: 'Consolas'; background: transparent;";
        const QString BtnNormal     = "QPushButton { background: transparent; border: 1px solid #555; color: #E0E0E0; border-radius: 6px; padding: 6px 12px; } QPushButton:hover { background: rgba(255,255,255,0.05); }";
    }

   namespace Light {
        const QString Global = R"(
            QWidget {
                background-color: #F5F7FA;
                color: #111111;
                font-family: 'Inter', 'Microsoft YaHei';
                font-size: 14px;
            }

            QWidget#Sidebar { background-color: #FFFFFF; border-right: 1px solid #E0E0E0; }
            QPushButton#SidebarBtn {
                text-align: left;
                padding: 12px 24px;
                border: none;
                border-radius: 6px;
                margin: 4px 8px;
                color: #333333;
                font-size: 18px;
                font-weight: normal;
                background-color: transparent;
            }
            QPushButton#SidebarBtn:hover {
                background-color: #EFF1F3;
                color: #111111;
            }
            QPushButton#SidebarBtn:checked {
                background-color: rgba(0, 150, 136, 0.15);
                color: #009688;
                font-weight: normal;
                border-left: 4px solid #009688;
            }

            QTableWidget {
                background-color: #FFFFFF;
                border: 1px solid #E0E0E0;
                color: #111111;
                gridline-color: #F0F0F0;
                selection-background-color: rgba(0, 150, 136, 0.15);
            }
            QHeaderView::section {
                background-color: #FAFAFA;
                color: #111111;
                font-weight: bold;
                border: none;
                border-bottom: 2px solid #DDDDDD;
                padding: 6px;
            }

            QTableWidget::item:selected,
            QTableWidget::item:selected:active,
            QTableWidget::item:selected:!active {
                background-color: rgba(0, 150, 136, 0.15);
                color: #111111;
                border: none;
            }

            QLineEdit, QTextEdit, QPlainTextEdit {
                background-color: #FFFFFF;
                border: 1px solid #CCCCCC;
                border-radius: 4px;
                padding: 6px;
                color: #111111; /* 输入框纯黑 */
            }
            QLineEdit:focus { border: 1px solid #009688; }
            QComboBox {
                background-color: #FFFFFF;
                border: 1px solid #CCCCCC;
                padding: 5px 10px;
                color: #111111;
            }
            QLabel { background: transparent; color: #111111; }
            QSplitter::handle { background: #CCCCCC; }
        )";

        const QString TabButton   = "QPushButton { background: transparent; color: #555; border: none; padding: 8px 25px; font-size: 14px; } QPushButton:checked { color: #009688; border-bottom: 3px solid #009688; font-weight: 800; }";
        const QString InfoBox     = "QFrame { background-color: #FFFFFF; border-left: 4px solid #009688; border-radius: 4px; padding: 15px; border: 1px solid #E5E5E5; }";

        const QString InfoTitle   = "font-weight: 800; font-size: 14px; margin-bottom: 5px; color: #111111; background: transparent;";
        const QString InfoContent = "font-size: 13px; color: #111111; background: transparent; font-weight: 500;";
        
        const QString BtnOutline  = "QPushButton { background: transparent; border: 1px solid #D93025; color: #D93025; border-radius: 4px; padding: 6px 12px; }";
        const QString ResourceTitle = "font-size: 15px; color: #111111; margin-bottom: 4px; background: transparent;";
        const QString ResourceValue = "font-size: 13px; color: #111111; opacity: 1.0; font-family: 'Consolas'; background: transparent;";
        const QString BtnNormal     = "QPushButton { background: #FFFFFF; border: 1px solid #AAA; color: #111111; border-radius: 6px; padding: 6px 12px; font-weight: 700; } QPushButton:hover { background: #F5F5F5; }";
    }
}