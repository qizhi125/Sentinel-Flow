#pragma once
#include <QWidget>
#include <QLabel>

class StatCard : public QWidget {
    Q_OBJECT
public:
    enum Type { Normal, Success, Warning, Danger };

    explicit StatCard(const QString &title, const QString &value, Type type = Normal, QWidget *parent = nullptr);

    void setValue(const QString &value);
    void setType(Type type);

private:
    QLabel *m_titleLabel;
    QLabel *m_valueLabel;
};