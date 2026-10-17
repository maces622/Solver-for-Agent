#ifndef GTO_API_STRATEGY_RESULT_PROVIDER_H
#define GTO_API_STRATEGY_RESULT_PROVIDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace GtoApi {

struct ResultTreeSummary
{
    QString rootNodeId = QStringLiteral("root");
    int nodeCount = 0;
};

struct NodeQueryResult
{
    QString nodeId;
    QJsonArray actions;
    int comboCount = 0;
    QJsonObject artifactContent;
};

class StrategyResultProvider : public QObject
{
    Q_OBJECT

public:
    explicit StrategyResultProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~StrategyResultProvider() override = default;

    virtual bool getTreeSummary(const QString &jobId,
                                ResultTreeSummary *summary,
                                QString *errorCode = nullptr,
                                QString *errorMessage = nullptr) = 0;
    virtual bool queryNode(const QString &jobId,
                           const QString &nodeId,
                           const QStringList &actionFilter,
                           int limit,
                           NodeQueryResult *result,
                           QString *errorCode = nullptr,
                           QString *errorMessage = nullptr) = 0;
};

} // namespace GtoApi

#endif // GTO_API_STRATEGY_RESULT_PROVIDER_H