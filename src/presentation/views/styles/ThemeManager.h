#pragma once
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include "global.h"

inline bool g_isDarkMode = true;

class ThemeManager {
public:
    static void applyTheme(QApplication& app, bool isDark) {
        g_isDarkMode = isDark;

        app.setStyle(QStyleFactory::create("Fusion"));
        QPalette p;
        if (isDark) {
            p.setColor(QPalette::Window, QColor("#1E1E1E"));
            p.setColor(QPalette::WindowText, QColor("#999999")); // 🚀 基础灰
            p.setColor(QPalette::Base, QColor("#2D2D2D"));
            p.setColor(QPalette::AlternateBase, QColor("#252526"));
            p.setColor(QPalette::ToolTipBase, QColor("#2D2D2D"));
            p.setColor(QPalette::ToolTipText, QColor("#999999")); // 🚀
            p.setColor(QPalette::Text, QColor("#999999")); // 🚀
            p.setColor(QPalette::Button, QColor("#333333"));
            p.setColor(QPalette::ButtonText, QColor("#999999")); // 🚀
            p.setColor(QPalette::Link, QColor("#00BFA5"));
            p.setColor(QPalette::Highlight, QColor(0, 191, 165, 40));
            p.setColor(QPalette::HighlightedText, QColor("#00BFA5"));
            p.setColor(QPalette::PlaceholderText, QColor("#555555")); // 极暗占位符
            p.setColor(QPalette::Mid, QColor("#444444"));
            p.setColor(QPalette::Midlight, QColor("#333333"));
        } else {
            p.setColor(QPalette::Window, QColor("#F5F7FA"));
            p.setColor(QPalette::WindowText, QColor("#444444")); // 🚀
            p.setColor(QPalette::Base, QColor("#FFFFFF"));
            p.setColor(QPalette::AlternateBase, QColor("#FAFAFA"));
            p.setColor(QPalette::ToolTipBase, QColor("#FFFFFF"));
            p.setColor(QPalette::ToolTipText, QColor("#444444")); // 🚀
            p.setColor(QPalette::Text, QColor("#444444")); // 🚀
            p.setColor(QPalette::Button, QColor("#FFFFFF"));
            p.setColor(QPalette::ButtonText, QColor("#444444")); // 🚀
            p.setColor(QPalette::Link, QColor("#009688"));
            p.setColor(QPalette::Highlight, QColor(0, 150, 136, 40));
            p.setColor(QPalette::HighlightedText, QColor("#009688"));
            p.setColor(QPalette::PlaceholderText, QColor("#AAAAAA")); // 极亮占位符
            p.setColor(QPalette::Mid, QColor("#CCCCCC"));
            p.setColor(QPalette::Midlight, QColor("#E0E0E0"));
        }
        app.setPalette(p);
        
        // 🚀 一次性应用全局 CSS 字典
        app.setStyleSheet(Style::getGlobalStyle());
    }
};