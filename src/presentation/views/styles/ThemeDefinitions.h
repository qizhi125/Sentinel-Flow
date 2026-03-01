#pragma once
#include <QString>

namespace ThemeDef {

    // 基础语义色彩常量 (供 C++ 渲染使用)
    const QString Accent      = "#00BFA5";
    const QString AccentLight = "#009688";
    const QString Danger      = "#FF3333";
    const QString Warning     = "#F1C40F";

    namespace Dark {
        const QString Global = R"(
            /* ================= 1. 全局基础 (Global Base) ================= */
            QWidget {
                background-color: #1E1E1E;
                color: #999999;
                font-family: 'Inter', 'Microsoft YaHei', sans-serif;
                font-size: 14px;
            }
            QToolTip { background-color: #2D2D2D; color: #999999; border: 1px solid #00BFA5; padding: 4px; }

            /* ================= 2. 布局与容器 (Containers & Cards) ================= */
            QWidget#Sidebar { background-color: #252526; border-right: 1px solid #333333; }
            QSplitter::handle { background-color: #333333; }
            QScrollArea { border: none; background: transparent; }
            QScrollArea > QWidget > QWidget { background: transparent; }

            /* 卡片基类 */
            QFrame#Card {
                background-color: #2D2D2D;
                border: 1px solid #333333;
                border-radius: 6px;
                border-left: 4px solid #00BFA5; 
            }
            QFrame#Card[status="danger"]  { border-left: 4px solid #FF3333; }
            QFrame#Card[status="warning"] { border-left: 4px solid #F1C40F; }
            QFrame#Card[status="neutral"] { border-left: 4px solid #999999; }
            
            QGroupBox { border: 1px solid #444; border-radius: 6px; margin-top: 10px; padding-top: 15px; font-weight: bold; color: #00BFA5; }
            QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; }

            /* ================= 3. 按钮系统 (Button System) ================= */
            QPushButton {
                background-color: #333333;
                border: 1px solid #555555;
                color: #999999;
                border-radius: 4px;
                padding: 6px 14px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #444444; border-color: #00BFA5; color: #E0E0E0; }
            QPushButton:pressed { background-color: #222222; }
            
            /* 侧边栏按钮 */
            QPushButton#SidebarBtn {
                text-align: left; padding: 12px 24px; border: none; border-radius: 6px;
                margin: 4px 8px; color: #999999; font-size: 16px; background-color: transparent;
            }
            QPushButton#SidebarBtn:hover { background-color: rgba(255, 255, 255, 0.05); color: #E0E0E0; }
            QPushButton#SidebarBtn:checked {
                background-color: rgba(0, 191, 165, 0.15); color: #00BFA5;
                border-left: 4px solid #00BFA5; border-radius: 0px 6px 6px 0px;
            }
            
            /* 动态属性按钮 */
            QPushButton[type="primary"] { background-color: #00BFA5; color: #111111; border: none; }
            QPushButton[type="primary"]:hover { background-color: #00E5C5; }
            QPushButton[type="danger"] { background-color: #FF3333; color: #FFFFFF; border: none; }
            QPushButton[type="danger"]:hover { background-color: #FF6666; }
            QPushButton[type="warning"] { background-color: #F1C40F; color: #111111; border: none; }
            QPushButton[type="warning"]:hover { background-color: #D4AC0D; }
            QPushButton[type="outline-danger"] { background-color: transparent; border: 1px solid #FF3333; color: #FF3333; }
            QPushButton[type="outline-danger"]:hover { background-color: rgba(255,51,51,0.1); }
            QPushButton[type="tab"] { background: transparent; color: #999999; border: none; padding: 8px 25px; font-size: 14px; } 
            QPushButton[type="tab"]:checked { color: #00BFA5; border-bottom: 3px solid #00BFA5; }

            /* ================= 4. 表单与输入 (Forms & Inputs) ================= */
            QLineEdit, QTextEdit, QPlainTextEdit {
                background-color: transparent; /* 🚀 变更为透明 */
                border: 1px solid #444;
                border-radius: 4px;
                padding: 6px 10px;
                color: #999999;
                selection-background-color: #00BFA5;
                selection-color: #111;
            }
            QLineEdit:focus, QTextEdit:focus { border: 1px solid #00BFA5; color: #E0E0E0; }
            QLineEdit::placeholder, QTextEdit::placeholder { color: #555555; }
            
            QComboBox { background-color: transparent; border: 1px solid #444; border-radius: 4px; padding: 5px 10px; color: #999999; }
            QComboBox:hover, QComboBox:focus { border: 1px solid #00BFA5; color: #E0E0E0; }
            QComboBox::drop-down { border: none; width: 24px; }
            QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #999999; margin-right: 8px; }

            QCheckBox, QRadioButton { spacing: 8px; font-weight: bold; background: transparent; }
            QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid #666; border-radius: 4px; background: transparent; }
            QRadioButton::indicator { border-radius: 9px; }
            QCheckBox::indicator:checked, QRadioButton::indicator:checked { border: 1px solid #00BFA5; background-color: rgba(0, 191, 165, 0.2); }

            /* ================= 5. 视图组件 (Tables, Trees, Scrollbars) ================= */
            QTableWidget, QTableView, QTreeWidget {
                background-color: transparent; border: none; color: #999999;
                gridline-color: #2D2D2D; selection-background-color: rgba(0, 191, 165, 0.15); selection-color: #00BFA5;
            }
            QHeaderView::section { background-color: #252526; color: #555555; border: none; border-bottom: 2px solid #333333; padding: 8px; font-weight: bold; text-transform: uppercase; }
            QTreeWidget::item { padding: 4px; border-bottom: 1px solid rgba(255,255,255,0.05); }
            QTreeWidget::item:selected { background-color: rgba(0, 191, 165, 0.15); color: #00BFA5; }

            QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
            QScrollBar::handle:vertical { background: #444444; border-radius: 4px; min-height: 20px; }
            QScrollBar::handle:vertical:hover { background: #00BFA5; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

            QProgressBar { background: #333; border: none; border-radius: 4px; text-align: center; color: transparent; }
            QProgressBar::chunk { background-color: #00BFA5; border-radius: 4px; }
            QProgressBar[status="danger"]::chunk { background-color: #FF3333; }
            QProgressBar[status="warning"]::chunk { background-color: #F1C40F; }
            QProgressBar[status="info"]::chunk { background-color: #3498DB; }
            QProgressBar[status="neutral"]::chunk { background-color: #9B59B6; }

            /* ================= 6. 专属定制字体 (Typography Roles) ================= */
            QLabel { background: transparent; color: #999999; }
            QLabel[role="title"] { font-size: 18px; font-weight: bold; color: #00BFA5; }
            QLabel[role="subtitle"] { font-size: 14px; font-weight: bold; color: #999999; }
            QLabel[role="value"] { font-size: 28px; font-weight: 800; font-family: 'JetBrains Mono'; color: #E0E0E0; }
            QLabel[role="hex"] { font-family: 'JetBrains Mono'; color: #E6DB74; font-size: 14px; }
            QTextEdit[role="console"] { font-family: 'JetBrains Mono'; font-size: 14px; color: #E6DB74; background-color: transparent; border: 1px solid #333; } /* 🚀 终端透明 */
        )";
    }

    namespace Light {
        const QString Global = R"(
            /* ================= 1. 全局基础 (Global Base) ================= */
            QWidget {
                background-color: #F5F7FA;
                color: #444444;
                font-family: 'Inter', 'Microsoft YaHei', sans-serif;
                font-size: 14px;
            }
            QToolTip { background-color: #FFFFFF; color: #444444; border: 1px solid #009688; padding: 4px; }

            /* ================= 2. 布局与容器 ================= */
            QWidget#Sidebar { background-color: #FFFFFF; border-right: 1px solid #E0E0E0; }
            QSplitter::handle { background-color: #CCCCCC; }
            QScrollArea { border: none; background: transparent; }
            QScrollArea > QWidget > QWidget { background: transparent; }

            QFrame#Card {
                background-color: #FFFFFF;
                border: 1px solid #E5E5E5;
                border-radius: 6px;
                border-left: 4px solid #009688; 
            }
            QFrame#Card[status="danger"]  { border-left: 4px solid #D93025; }
            QFrame#Card[status="warning"] { border-left: 4px solid #E67E22; }
            QFrame#Card[status="neutral"] { border-left: 4px solid #444444; }

            QGroupBox { border: 1px solid #CCC; border-radius: 6px; margin-top: 10px; padding-top: 15px; font-weight: bold; color: #009688; }
            QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; }

            /* ================= 3. 按钮系统 ================= */
            QPushButton {
                background-color: #FFFFFF; border: 1px solid #CCCCCC; color: #444444;
                border-radius: 4px; padding: 6px 14px; font-weight: bold;
            }
            QPushButton:hover { background-color: #F0F2F5; border-color: #009688; color: #111111; }
            QPushButton:pressed { background-color: #E0E0E0; }
            
            QPushButton#SidebarBtn {
                text-align: left; padding: 12px 24px; border: none; border-radius: 6px;
                margin: 4px 8px; color: #444444; font-size: 16px; background-color: transparent;
            }
            QPushButton#SidebarBtn:hover { background-color: #EFF1F3; color: #111111; }
            QPushButton#SidebarBtn:checked {
                background-color: rgba(0, 150, 136, 0.1); color: #009688;
                border-left: 4px solid #009688; border-radius: 0px 6px 6px 0px;
            }
            
            QPushButton[type="primary"] { background-color: #009688; color: #FFFFFF; border: none; }
            QPushButton[type="primary"]:hover { background-color: #00BFA5; }
            QPushButton[type="danger"] { background-color: #D93025; color: #FFFFFF; border: none; }
            QPushButton[type="danger"]:hover { background-color: #FF4444; }
            QPushButton[type="warning"] { background-color: #F1C40F; color: #111111; border: none; }
            QPushButton[type="warning"]:hover { background-color: #D4AC0D; }
            QPushButton[type="outline-danger"] { background-color: transparent; border: 1px solid #D93025; color: #D93025; }
            QPushButton[type="outline-danger"]:hover { background-color: rgba(217,48,37,0.1); }
            QPushButton[type="tab"] { background: transparent; color: #444444; border: none; padding: 8px 25px; font-size: 14px; } 
            QPushButton[type="tab"]:checked { color: #009688; border-bottom: 3px solid #009688; }

            /* ================= 4. 表单与输入 ================= */
            QLineEdit, QTextEdit, QPlainTextEdit {
                background-color: transparent; /* 🚀 变更为透明 */
                border: 1px solid #CCCCCC;
                border-radius: 4px; padding: 6px 10px; color: #444444;
                selection-background-color: rgba(0, 150, 136, 0.2); selection-color: #111;
            }
            QLineEdit:focus, QTextEdit:focus { border: 1px solid #009688; color: #111111; }
            QLineEdit::placeholder, QTextEdit::placeholder { color: #AAAAAA; }
            
            QComboBox { background-color: transparent; border: 1px solid #CCCCCC; border-radius: 4px; padding: 5px 10px; color: #444444; }
            QComboBox:hover, QComboBox:focus { border: 1px solid #009688; color: #111111; }
            QComboBox::drop-down { border: none; width: 24px; }
            QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #444444; margin-right: 8px; }

            QCheckBox, QRadioButton { spacing: 8px; font-weight: bold; background: transparent; }
            QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid #CCC; border-radius: 4px; background: transparent; }
            QRadioButton::indicator { border-radius: 9px; }
            QCheckBox::indicator:checked, QRadioButton::indicator:checked { border: 1px solid #009688; background-color: rgba(0, 150, 136, 0.2); }

            /* ================= 5. 视图组件 ================= */
            QTableWidget, QTableView, QTreeWidget {
                background-color: transparent; border: none; color: #444444;
                gridline-color: #F0F0F0; selection-background-color: rgba(0, 150, 136, 0.15); selection-color: #009688; alternate-background-color: #FAFAFA;
            }
            QHeaderView::section { background-color: #F5F7FA; color: #999999; border: none; border-bottom: 2px solid #DDDDDD; padding: 8px; font-weight: bold; text-transform: uppercase; }
            QTreeWidget::item { padding: 4px; border-bottom: 1px solid rgba(0,0,0,0.05); }
            QTreeWidget::item:selected { background-color: rgba(0, 150, 136, 0.15); color: #009688; }

            QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
            QScrollBar::handle:vertical { background: #CCCCCC; border-radius: 4px; min-height: 20px; }
            QScrollBar::handle:vertical:hover { background: #009688; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

            QProgressBar { background: #E0E0E0; border: none; border-radius: 4px; text-align: center; color: transparent; }
            QProgressBar::chunk { background-color: #009688; border-radius: 4px; }
            QProgressBar[status="danger"]::chunk { background-color: #D93025; }
            QProgressBar[status="warning"]::chunk { background-color: #E67E22; }
            QProgressBar[status="info"]::chunk { background-color: #3498DB; }
            QProgressBar[status="neutral"]::chunk { background-color: #9B59B6; }

            /* ================= 6. 定制字体 ================= */
            QLabel { background: transparent; color: #444444; }
            QLabel[role="title"] { font-size: 18px; font-weight: bold; color: #009688; }
            QLabel[role="subtitle"] { font-size: 14px; font-weight: bold; color: #444444; }
            QLabel[role="value"] { font-size: 28px; font-weight: 800; font-family: 'JetBrains Mono'; color: #111111; }
            QLabel[role="hex"] { font-family: 'JetBrains Mono'; color: #111111; font-size: 14px; }
            QTextEdit[role="console"] { font-family: 'JetBrains Mono'; font-size: 14px; color: #111111; background-color: transparent; border: 1px solid #CCC; } /* 🚀 终端透明 */
        )";
    }
}