#pragma once
#include "presentation/views/components/StatCard.h"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

class IdsRulesTab : public QWidget {
    Q_OBJECT
public:
    explicit IdsRulesTab(QWidget *parent = nullptr);

private slots:
    void onAddRule();
    void onDeleteRule();
    void onImportRules();
    void onClearRulesClicked();
    void onFilterTreeClicked(const QTreeWidgetItem *item, int column);

private:
    void setupUi();
    void updateStatCards() const;
    void loadPersistedData() const;

    StatCard *cardTotalRules;
    StatCard *cardHighThreats;
    StatCard *cardIntercepted;
    QTreeWidget *ruleFilterTree;

    QTableWidget *idsTable;
    QLineEdit *ruleNameInput;
    QLineEdit *rulePatternInput;
    QPushButton *btnAddRule;
    QPushButton *btnDeleteRule;
    QPushButton *btnImportRules;
    QPushButton *btnClearRules;

    int globalSkippedCount = 0;
};