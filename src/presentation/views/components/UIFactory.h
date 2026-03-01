#pragma once
#include <QWidget>
#include <QString>

class UIFactory {
public:
    static QWidget* createInfoBox(const QString& title, const QString& text, QWidget* parent = nullptr);
};