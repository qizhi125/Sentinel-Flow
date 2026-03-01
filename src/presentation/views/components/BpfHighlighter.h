#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>

class BpfHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit BpfHighlighter(QTextDocument *parent = nullptr);

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat keywordFormat;    // tcp, udp, icmp, ip, arp 等
    QTextCharFormat operatorFormat;   // and, or, not
    QTextCharFormat numberFormat;     // 端口号 80, 443 等
    QTextCharFormat ipFormat;         // IP 地址正则

protected:
    void highlightBlock(const QString &text) override;
};