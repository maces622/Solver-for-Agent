#ifndef GTO_API_GUI_NODE_PATH_H
#define GTO_API_GUI_NODE_PATH_H

#include "include/Card.h"
#include "include/nodes/GameTreeNode.h"

#include <QString>
#include <QVector>
#include <memory>

class ActionNode;

namespace GtoApi {

// Resolves a deterministic API node id against the concrete GameTree. The
// returned path starts at the root and contains one entry per segment. Chance
// card segments select a card but the underlying GameTree stores one child for
// all cards, so the next path entry is that single child.
bool resolveGuiNodePath(const shared_ptr<GameTreeNode> &root,
                        const QString &nodeId,
                        QVector<shared_ptr<GameTreeNode>> *path,
                        QVector<Card> *chanceCards,
                        QString *errorMessage = nullptr);

// Resolves a node id and requires the final node to be an ActionNode.
bool resolveGuiActionNode(const shared_ptr<GameTreeNode> &root,
                          const QString &nodeId,
                          shared_ptr<ActionNode> *actionNode,
                          QVector<Card> *chanceCards,
                          QString *errorMessage = nullptr);

} // namespace GtoApi

#endif // GTO_API_GUI_NODE_PATH_H
