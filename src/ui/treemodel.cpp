#include "include/ui/treemodel.h"

#include <QStringList>

TreeModel::TreeModel(QSolverJob * data, QObject *parent)
    : QAbstractItemModel(parent)
{
    this->qSolverJob = data;
    setupModelData();
}

TreeModel::~TreeModel()
{
    delete rootItem;
}

int TreeModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<TreeItem*>(parent.internalPointer())->columnCount();
    return rootItem->columnCount();
}

QVariant TreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role != Qt::DisplayRole)
        return QVariant();

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

    return item->data();
}

Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant TreeModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return rootItem->data();

    return QVariant();
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeItem *parentItem;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    TreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex TreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
    TreeItem *parentItem = childItem->parentItem();

    if (parentItem == rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int TreeModel::rowCount(const QModelIndex &parent) const
{
    TreeItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

void TreeModel::reGenerateTreeItem(GameTreeNode::GameRound round,TreeItem* node_to_process){
    const shared_ptr<GameTreeNode> gameTreeNode = node_to_process->m_treedata.lock();
    if(gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(gameTreeNode);
        vector<shared_ptr<GameTreeNode>>& childrens = actionNode->getChildrens();
        for(shared_ptr<GameTreeNode> one_child:childrens){
            TreeItem * child_node = new TreeItem(one_child,node_to_process);
            node_to_process->insertChild(child_node);
            if(one_child->getRound() != round){
                continue;
            }
            this->reGenerateTreeItem(round,child_node);
        }
    }
    else if(gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::CHANCE){
        shared_ptr<ChanceNode> chanceNode = dynamic_pointer_cast<ChanceNode>(gameTreeNode);
        TreeItem * child_node = new TreeItem(chanceNode->getChildren(),node_to_process);
        node_to_process->insertChild(child_node);
        this->reGenerateTreeItem(round,child_node);
    }
}

void TreeModel::setupModelData()
{
    PokerSolver * solver;
    if(this->qSolverJob->mode == QSolverJob::Mode::HOLDEM){
        solver = &(this->qSolverJob->ps_holdem);
    }else if(this->qSolverJob->mode == QSolverJob::Mode::SHORTDECK){
        solver = &(this->qSolverJob->ps_shortdeck);
    }else{
        throw runtime_error("holdem mode incorrect");
    }

    if(solver->get_game_tree() == nullptr || solver->get_game_tree()->getRoot() == nullptr){
        return;
    }

    GameTreeNode::GameRound round =	solver->get_game_tree()->getRoot()->getRound();

    this->rootItem = new TreeItem(solver->get_game_tree()->getRoot());
    TreeItem* ti = new TreeItem(solver->get_game_tree()->getRoot(),this->rootItem);
    rootItem->insertChild(ti);
    this->reGenerateTreeItem(round,ti);
}



TreeItem *TreeModel::findDirectChild(TreeItem *parent,
                                     const shared_ptr<GameTreeNode> &node) const
{
    if (!parent || !node) {
        return nullptr;
    }
    for (int row = 0; row < parent->childCount(); ++row) {
        TreeItem *child = parent->child(row);
        if (child && child->m_treedata.lock().get() == node.get()) {
            return child;
        }
    }
    return nullptr;
}

QModelIndex TreeModel::indexForTreeItem(TreeItem *item) const
{
    if (!item || !rootItem) {
        return QModelIndex();
    }

    QVector<TreeItem *> chain;
    TreeItem *cursor = item;
    while (cursor && cursor != rootItem) {
        chain.prepend(cursor);
        cursor = cursor->parentItem();
    }
    if (chain.isEmpty()) {
        return QModelIndex();
    }

    QModelIndex parent;
    QModelIndex current;
    for (TreeItem *entry : chain) {
        current = index(entry->row(), 0, parent);
        if (!current.isValid()) {
            return QModelIndex();
        }
        parent = current;
    }
    return current;
}

QModelIndex TreeModel::indexForNodePath(const QVector<shared_ptr<GameTreeNode>> &path,
                                        QString *errorMessage)
{
    if (!rootItem || path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Game tree is not available");
        }
        return QModelIndex();
    }

    TreeItem *current = findDirectChild(rootItem, path.first());
    if (!current) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node path root is not in the game tree");
        }
        return QModelIndex();
    }

    for (int depth = 1; depth < path.size(); ++depth) {
        const shared_ptr<GameTreeNode> &nextNode = path.at(depth);
        TreeItem *child = findDirectChild(current, nextNode);
        if (!child && current->childCount() == 0 && nextNode) {
            reGenerateTreeItem(nextNode->getRound(), current);
            child = findDirectChild(current, nextNode);
        }
        if (!child) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Node path is not present in the game tree");
            }
            return QModelIndex();
        }
        current = child;
    }
    return indexForTreeItem(current);
}

QString TreeModel::nodeIdForIndex(const QModelIndex &index,
                                  const QString &turnCard,
                                  const QString &riverCard,
                                  QString *errorMessage) const
{
    if (!index.isValid() || !rootItem) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected tree node is not available");
        }
        return QString();
    }

    auto *selected = static_cast<TreeItem *>(index.internalPointer());
    QVector<TreeItem *> chain;
    TreeItem *cursor = selected;
    while (cursor && cursor != rootItem) {
        chain.prepend(cursor);
        cursor = cursor->parentItem();
    }
    if (chain.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected tree node is not in the game tree");
        }
        return QString();
    }

    QStringList segments;
    for (int depth = 1; depth < chain.size(); ++depth) {
        const shared_ptr<GameTreeNode> parent = chain.at(depth - 1)->m_treedata.lock();
        const shared_ptr<GameTreeNode> child = chain.at(depth)->m_treedata.lock();
        if (!parent || !child) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Selected tree node has expired");
            }
            return QString();
        }

        if (parent->getType() == GameTreeNode::ACTION) {
            const auto action = std::dynamic_pointer_cast<ActionNode>(parent);
            if (!action) {
                break;
            }
            const vector<shared_ptr<GameTreeNode>> &children = action->getChildrens();
            int childIndex = -1;
            for (int i = 0; i < static_cast<int>(children.size()); ++i) {
                if (children.at(i).get() == child.get()) {
                    childIndex = i;
                    break;
                }
            }
            if (childIndex < 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Selected node is not an action child");
                }
                return QString();
            }
            segments.append(QStringLiteral("a%1").arg(childIndex));
        } else if (parent->getType() == GameTreeNode::CHANCE) {
            const auto chance = std::dynamic_pointer_cast<ChanceNode>(parent);
            QString cardText;
            if (chance && chance->getRound() == GameTreeNode::GameRound::RIVER) {
                cardText = riverCard;
            } else {
                cardText = turnCard;
            }
            if (cardText.isEmpty()) {
                if (chance && !chance->getCards().empty()) {
                    Card first = chance->getCards().front();
                    cardText = QString::fromStdString(first.toString());
                }
            }
            if (cardText.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("No chance card is selected");
                }
                return QString();
            }
            segments.append(QStringLiteral("c%1").arg(cardText));
        } else {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Selected node is below a terminal node");
            }
            return QString();
        }
    }

    if (segments.isEmpty()) {
        return QStringLiteral("root");
    }
    return QStringLiteral("root/") + segments.join(QLatin1Char('/'));
}

void TreeModel::clicked_event(const QModelIndex & index){
}
