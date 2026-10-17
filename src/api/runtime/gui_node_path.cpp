#include "gui_node_path.h"

#include "src/api/node_path.h"

#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"

#include <QStringList>

namespace GtoApi {

bool resolveGuiNodePath(const shared_ptr<GameTreeNode> &root,
                        const QString &nodeId,
                        QVector<shared_ptr<GameTreeNode>> *path,
                        QVector<Card> *chanceCards,
                        QString *errorMessage)
{
    if (!root || !path) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Game tree is not available");
        }
        return false;
    }
    if (!isValidNodePathSyntax(nodeId, errorMessage)) {
        return false;
    }

    path->clear();
    if (chanceCards) {
        chanceCards->clear();
    }
    path->append(root);

    const QStringList segments = nodeId.split(QLatin1Char('/'));
    shared_ptr<GameTreeNode> current = root;
    for (int index = 1; index < segments.size(); ++index) {
        const QString segment = segments.at(index);
        if (!current) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Node path is incomplete");
            }
            return false;
        }

        if (current->getType() == GameTreeNode::ACTION) {
            bool ok = false;
            const int childIndex = segment.mid(1).toInt(&ok);
            const auto action = std::dynamic_pointer_cast<ActionNode>(current);
            if (!ok || childIndex < 0 || !action
                || childIndex >= static_cast<int>(action->getChildrens().size())) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Invalid action child index: %1").arg(segment);
                }
                return false;
            }
            current = action->getChildrens().at(childIndex);
        } else if (current->getType() == GameTreeNode::CHANCE) {
            QString cardText = segment.mid(1);
            if (cardText.startsWith(QLatin1Char(':'))) {
                cardText = cardText.mid(1);
            }
            const auto chance = std::dynamic_pointer_cast<ChanceNode>(current);
            bool found = false;
            if (chance) {
                for (const Card &card : chance->getCards()) {
                    Card cardCopy = card;
                    if (QString::fromStdString(cardCopy.toString()).compare(
                            cardText, Qt::CaseInsensitive) == 0) {
                        if (chanceCards) {
                            chanceCards->append(cardCopy);
                        }
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Card %1 is not available at this chance node")
                                        .arg(cardText);
                }
                return false;
            }
            current = chance->getChildren();
        } else {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Node path cannot continue through a terminal node");
            }
            return false;
        }
        path->append(current);
    }
    return true;
}

bool resolveGuiActionNode(const shared_ptr<GameTreeNode> &root,
                          const QString &nodeId,
                          shared_ptr<ActionNode> *actionNode,
                          QVector<Card> *chanceCards,
                          QString *errorMessage)
{
    QVector<shared_ptr<GameTreeNode>> path;
    if (!resolveGuiNodePath(root, nodeId, &path, chanceCards, errorMessage)) {
        return false;
    }
    const shared_ptr<GameTreeNode> current = path.isEmpty() ? nullptr : path.last();
    if (!current || current->getType() != GameTreeNode::ACTION) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node id does not resolve to an action node");
        }
        return false;
    }
    if (actionNode) {
        *actionNode = std::dynamic_pointer_cast<ActionNode>(current);
    }
    return true;
}

} // namespace GtoApi
