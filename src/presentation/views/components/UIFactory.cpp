#include "UIFactory.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPalette>

QWidget* UIFactory::createInfoBox(const QString& title, const QString& text, QWidget* parent) {
    auto *box = new QWidget(parent);
    box->setObjectName("InfoBox");

    QColor accent = box->palette().color(QPalette::Highlight);
    QString accentHex = accent.name();
    QString bgRgba = QString("rgba(%1, %2, %3, 0.10)").arg(accent.red()).arg(accent.green()).arg(accent.blue());

    box->setStyleSheet(QString(
        "#InfoBox { background-color: %1; border-left: 4px solid %2; border-radius: 4px; }"
    ).arg(bgRgba, accentHex));

    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(15, 10, 15, 10);

    auto *lblTitle = new QLabel(title);
    lblTitle->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 14px;").arg(accentHex));

    auto *lblText = new QLabel(text);
    lblText->setWordWrap(true);
    lblText->setStyleSheet("color: #888888; font-size: 12px; line-height: 1.4;");

    layout->addWidget(lblTitle);
    layout->addWidget(lblText);

    return box;
}