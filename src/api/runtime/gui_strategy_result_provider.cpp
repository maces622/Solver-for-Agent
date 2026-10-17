#include "gui_strategy_result_provider.h"

#include "include/GameTree.h"
#include "include/solver/Solver.h"
#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"
#include "include/runtime/PokerSolver.h"
#include "include/runtime/qsolverjob.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <vector>
#include <cmath>

namespace GtoApi {

namespace {

QString canonicalActionName(GameActions &action)
{
    QString name = QString::fromStdString(action.toString()).toLower();
    name.replace(QLatin1Char(' '), QLatin1Char('_'));
    return name;
}

bool resolveActionNode(const shared_ptr<GameTreeNode> &root,
                       const QString &nodeId,
                       shared_ptr<ActionNode> *actionNode,
                       QVector<Card> *chanceCards,
                       QString *errorMessage)
{
    const QStringList segments = nodeId.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty() || segments.first() != QStringLiteral("root")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node id must start with root");
        }
        return false;
    }

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
            if (!segment.startsWith(QLatin1Char('a'))) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Action node segment must start with 'a': %1")
                                        .arg(segment);
                }
                return false;
            }
            bool ok = false;
            const int childIndex = segment.mid(1).toInt(&ok);
            const auto action = std::dynamic_pointer_cast<ActionNode>(current);
            if (!ok || childIndex < 0
                || childIndex >= static_cast<int>(action->getChildrens().size())) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Invalid action child index: %1").arg(segment);
                }
                return false;
            }
            current = action->getChildrens().at(childIndex);
        } else if (current->getType() == GameTreeNode::CHANCE) {
            if (!segment.startsWith(QLatin1Char('c'))) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Chance node segment must start with 'c': %1")
                                        .arg(segment);
                }
                return false;
            }
            QString cardText = segment.mid(1);
            if (cardText.startsWith(QLatin1Char(':'))) {
                cardText = cardText.mid(1);
            }
            const auto chance = std::dynamic_pointer_cast<ChanceNode>(current);
            bool found = false;
            for (const Card &card : chance->getCards()) {
                Card cardCopy = card;
                if (QString::fromStdString(cardCopy.toString()).compare(
                        cardText, Qt::CaseInsensitive) == 0) {
                    chanceCards->append(cardCopy);
                    found = true;
                    break;
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
    }

    if (!current || current->getType() != GameTreeNode::ACTION) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node id does not resolve to an action node");
        }
        return false;
    }
    *actionNode = std::dynamic_pointer_cast<ActionNode>(current);
    return true;
}

} // namespace

GuiStrategyResultProvider::GuiStrategyResultProvider(QSolverJob *solverJob,
                                                     QObject *parent)
    : StrategyResultProvider(parent)
    , m_solverJob(solverJob)
{
}

bool GuiStrategyResultProvider::getTreeSummary(const QString &,
                                               ResultTreeSummary *summary,
                                               QString *errorCode,
                                               QString *errorMessage)
{
    if (!m_solverJob) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_NOT_READY");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solver job is not available");
        }
        return false;
    }
    PokerSolver *solver = m_solverJob->get_solver();
    if (!solver || !solver->get_game_tree() || !solver->get_game_tree()->getRoot()) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_NOT_READY");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Game tree is not available");
        }
        return false;
    }
    summary->rootNodeId = QStringLiteral("root");
    summary->nodeCount = solver->get_game_tree()->getRoot()->subtree_size;
    return true;
}bool GuiStrategyResultProvider::queryNode(const QString &,
                                          const QString &nodeId,
                                          const QStringList &actionFilter,
                                          int limit,
                                          NodeQueryResult *result,
                                          QString *errorCode,
                                          QString *errorMessage)
{
    if (!result) {
        if (errorCode) {
            *errorCode = QStringLiteral("INVALID_REQUEST");
        }
        return false;
    }
    if (!m_solverJob || m_solverJob->isRunning()) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_NOT_READY");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve result is not ready");
        }
        return false;
    }

    PokerSolver *pokerSolver = m_solverJob->get_solver();
    if (!pokerSolver || !pokerSolver->get_solver() || !pokerSolver->get_game_tree()) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_NOT_READY");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solver result is not available");
        }
        return false;
    }

    shared_ptr<ActionNode> actionNode;
    QVector<Card> chanceCards;
    if (!resolveActionNode(pokerSolver->get_game_tree()->getRoot(),
                           nodeId, &actionNode, &chanceCards, errorMessage)) {
        if (errorCode) {
            *errorCode = QStringLiteral("NODE_NOT_FOUND");
        }
        return false;
    }

    try {
        const std::vector<Card> chanceCardVector(chanceCards.begin(), chanceCards.end());
        const std::vector<std::vector<std::vector<float>>> strategy =
            pokerSolver->get_solver()->get_strategy(actionNode, chanceCardVector);
        const std::vector<std::vector<std::vector<float>>> evs =
            pokerSolver->get_solver()->get_evs(actionNode, chanceCardVector);

        std::vector<GameActions> &actions = actionNode->getActions();
        const int actionCount = static_cast<int>(actions.size());
        QVector<double> frequencySums(actionCount, 0.0);
        QVector<double> evSums(actionCount, 0.0);
        int comboCount = 0;

        QJsonArray actionNames;
        for (GameActions &action : actions) {
            actionNames.append(canonicalActionName(action));
        }

        QJsonArray artifactCombos;
        for (int first = 0; first < 52; ++first) {
            for (int second = 0; second < 52; ++second) {
                const std::vector<float> &comboStrategy = strategy[first][second];
                if (static_cast<int>(comboStrategy.size()) != actionCount) {
                    continue;
                }
                ++comboCount;

                QJsonArray strategyJson;
                QJsonArray evJson;
                for (int actionIndex = 0; actionIndex < actionCount; ++actionIndex) {
                    const double frequency = comboStrategy[actionIndex];
                    frequencySums[actionIndex] += frequency;
                    strategyJson.append(frequency);
                    if (actionIndex < static_cast<int>(evs[first][second].size())) {
                        const double ev = evs[first][second][actionIndex];
                        evSums[actionIndex] += ev;
                        evJson.append(ev);
                    }
                }

                QJsonObject combo;
                combo.insert(QStringLiteral("cards"),
                             QString::fromStdString(Card::intCard2Str(first)
                                                    + Card::intCard2Str(second)));
                combo.insert(QStringLiteral("strategy"), strategyJson);
                combo.insert(QStringLiteral("ev"), evJson);
                artifactCombos.append(combo);
            }
        }

        if (comboCount == 0 || actionCount == 0) {
            if (errorCode) {
                *errorCode = QStringLiteral("RESULT_EMPTY");
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("No strategy data is available for this node");
            }
            return false;
        }

        QSet<QString> filter;
        for (QString action : actionFilter) {
            action = action.trimmed().toLower();
            action.replace(QLatin1Char(' '), QLatin1Char('_'));
            if (!action.isEmpty()) {
                filter.insert(action);
            }
        }
        const int actionLimit = limit > 0 ? qMin(limit, 100) : 100;
        QJsonArray actionSummaries;
        int emitted = 0;
        for (int actionIndex = 0; actionIndex < actionCount && emitted < actionLimit;
             ++actionIndex) {
            const QString name = canonicalActionName(actions[actionIndex]);
            if (!filter.isEmpty() && !filter.contains(name)) {
                continue;
            }
            QJsonObject action;
            action.insert(QStringLiteral("action"), name);
            action.insert(QStringLiteral("frequency"),
                          frequencySums[actionIndex] / comboCount);
            action.insert(QStringLiteral("ev"), evSums[actionIndex] / comboCount);
            actionSummaries.append(action);
            ++emitted;
        }

        QJsonObject artifactContent;
        artifactContent.insert(QStringLiteral("nodeId"), nodeId);
        artifactContent.insert(QStringLiteral("actions"), actionNames);
        artifactContent.insert(QStringLiteral("combos"), artifactCombos);

        result->nodeId = nodeId;
        result->actions = actionSummaries;
        result->comboCount = comboCount;
        result->artifactContent = artifactContent;
        return true;
    } catch (const std::exception &error) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_QUERY_FAILED");
        }
        if (errorMessage) {
            *errorMessage = QString::fromLocal8Bit(error.what());
        }
        return false;
    }
}

} // namespace GtoApi