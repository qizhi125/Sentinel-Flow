#pragma once
#include <QWidget>

class ThemeablePage : public QWidget {
public:
    explicit ThemeablePage(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual void onThemeChanged() = 0;
    virtual ~ThemeablePage() = default;
};