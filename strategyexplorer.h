#ifndef STRATEGYEXPLORER_H
#define STRATEGYEXPLORER_H

#include <QDialog>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QMouseEvent>

#include "include/runtime/qsolverjob.h"
#include "QItemSelection"
#include "include/ui/worditemdelegate.h"
#include "include/ui/tablestrategymodel.h"
#include "include/ui/strategyitemdelegate.h"
#include "include/ui/detailwindowsetting.h"
#include "include/Card.h"
#include "include/ui/detailviewermodel.h"
#include "include/ui/detailitemdelegate.h"
#include "include/ui/roughstrategyviewermodel.h"
#include "include/ui/roughstrategyitemdelegate.h"
#include "include/nodes/GameTreeNode.h"
#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"
#include "include/nodes/TerminalNode.h"
#include "include/nodes/ShowdownNode.h"

namespace Ui {
class StrategyExplorer;
}

class StrategyExplorer : public QDialog
{
    Q_OBJECT

public:
    explicit StrategyExplorer(QWidget *parent = 0,QSolverJob * qSolverJob=nullptr);
    ~StrategyExplorer();

    // Focuses one deterministic API tree node by expanding the materialized
    // tree as needed, selecting it, and refreshing the strategy views.
    bool focusNode(const QString &nodeId, QString *errorMessage = nullptr);
    QString currentNodeId(QString *errorMessage = nullptr) const;

signals:
    void nodeSelected(const QString &nodeId);

private:
    DetailWindowSetting detailWindowSetting;
    QTimer *timer;
    Ui::StrategyExplorer *ui;
    QSolverJob * qSolverJob;
    StrategyItemDelegate * delegate_strategy;
    TableStrategyModel * tableStrategyModel;
    DetailViewerModel * detailViewerModel;
    DetailItemDelegate * detailItemItemDelegate;
    RoughStrategyViewerModel * roughStrategyViewerModel;
    RoughStrategyItemDelegate * roughStrategyItemDelegate;
    vector<Card> cards;
    bool updatingFocus = false;
    void process_treeclick(TreeItem* treeitem);
    void process_board(TreeItem* treeitem);
    void emitNodeSelected();
public slots:
    void item_expanded(const QModelIndex& index);
    void item_clicked(const QModelIndex& index);
    void selection_changed(const QItemSelection &selected,
                                            const QItemSelection &deselected);
private slots:
    void on_turnCardBox_currentIndexChanged(int index);
    void on_riverCardBox_currentIndexChanged(int index);
    void update_second();
    void onMouseMoveEvent(int i,int j);
    void on_strategyModeButtom_clicked();
    void on_ipRangeButtom_clicked();
    void on_oopRangeButtom_clicked();
    void on_evModeButtom_clicked();
    void on_evOnlyModeButtom_clicked();
};

#endif // STRATEGYEXPLORER_H
