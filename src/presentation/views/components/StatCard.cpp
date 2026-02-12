#include "presentation/views/components/StatCard.h"
#include <QVBoxLayout>

StatCard::StatCard(const QString &title, const QString &value, Type type, QWidget *parent)
    : QWidget(parent)
{
    // 🔥 关键修正：设置 ID，让 global.h 接管背景色
    this->setObjectName("Card");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    // 标题
    m_titleLabel = new QLabel(title, this);
    // 使用 subtitle 角色，颜色会自动跟随主题（深色变灰，亮色变深灰）
    m_titleLabel->setProperty("role", "subtitle");
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 12px; letter-spacing: 0.5px; text-transform: uppercase;");

    // 数值
    m_valueLabel = new QLabel(value, this);
    // 只设置字号和粗细，颜色留给全局样式控制
    m_valueLabel->setStyleSheet("font-size: 28px; font-weight: 800;");

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_valueLabel);
    layout->addStretch();

    // 初始化状态颜色
    setType(type);
}

void StatCard::setValue(const QString &value) {
    m_valueLabel->setText(value);
}

void StatCard::setType(Type type) {
    // 我们只改变标题的颜色来指示状态，背景色保持不动
    QString color;
    switch (type) {
    case Normal:  color = "#888888"; break; // 默认灰
    case Success: color = "#2ecc71"; break; // 绿
    case Warning: color = "#f1c40f"; break; // 黄
    case Danger:  color = "#e74c3c"; break; // 红
    }

    // 强制刷新标题颜色
    m_titleLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 12px; letter-spacing: 0.5px; text-transform: uppercase;").arg(color));
}