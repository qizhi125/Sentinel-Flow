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
            p.setColor(QPalette::WindowText, QColor("#E0E0E0"));
            p.setColor(QPalette::Base, QColor("#2D2D2D"));
            p.setColor(QPalette::AlternateBase, QColor("#383838"));
            p.setColor(QPalette::ToolTipBase, QColor("#E0E0E0"));
            p.setColor(QPalette::ToolTipText, QColor("#E0E0E0"));
            p.setColor(QPalette::Text, QColor("#E0E0E0"));
            p.setColor(QPalette::Button, QColor("#1E1E1E"));
            p.setColor(QPalette::ButtonText, QColor("#E0E0E0"));
            p.setColor(QPalette::BrightText, Qt::red);
            p.setColor(QPalette::Link, QColor("#00BFA5"));
            p.setColor(QPalette::Highlight, QColor("#00BFA5"));
            p.setColor(QPalette::HighlightedText, Qt::black);
            p.setColor(QPalette::PlaceholderText, QColor("#888888"));

            p.setColor(QPalette::Mid, QColor("#444444")); 
            p.setColor(QPalette::Midlight, QColor("#333333")); 
            p.setColor(QPalette::Dark, QColor("#111111")); 
        } else {
            p.setColor(QPalette::Window, QColor("#F5F7FA"));  
            p.setColor(QPalette::WindowText, QColor("#111111")); 
            p.setColor(QPalette::Base, QColor("#FFFFFF"));  
            p.setColor(QPalette::AlternateBase, QColor("#F0F2F5"));
            p.setColor(QPalette::ToolTipBase, QColor("#FFFFFF"));
            p.setColor(QPalette::ToolTipText, QColor("#111111"));
            p.setColor(QPalette::Text, QColor("#111111"));  
            p.setColor(QPalette::Button, QColor("#FFFFFF"));
            p.setColor(QPalette::ButtonText, QColor("#111111"));
            p.setColor(QPalette::Link, QColor("#009688"));
            p.setColor(QPalette::Highlight, QColor("#009688"));
            p.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
            p.setColor(QPalette::PlaceholderText, QColor("#555555"));

            p.setColor(QPalette::Mid, QColor("#888888"));
            p.setColor(QPalette::Midlight, QColor("#CCCCCC"));
            p.setColor(QPalette::Dark, QColor("#BBBBBB"));
        }
        app.setPalette(p);
        app.setStyleSheet(Style::getGlobalStyle());
    }
};