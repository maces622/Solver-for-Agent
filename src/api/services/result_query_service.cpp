#include "result_query_service.h"

#include "artifact_store.h"
#include "strategy_result_provider.h"

namespace GtoApi {

ResultQueryService::ResultQueryService(QObject *parent)
    : QObject(parent)
    , m_artifactStore(new ArtifactStore(this))
{
}

void ResultQueryService::setProvider(StrategyResultProvider *provider)
{
    m_provider = provider;
}

StrategyResultProvider *ResultQueryService::provider() const
{
    return m_provider;
}

ArtifactStore *ResultQueryService::artifactStore() const
{
    return m_artifactStore;
}

bool ResultQueryService::getSummary(const QString &jobId,
                                    const JobStatus &status,
                                    QJsonObject *result,
                                    QString *errorCode,
                                    QString *errorMessage)
{
    if (!result) {
        return false;
    }
    if (!m_provider) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_PROVIDER_UNAVAILABLE");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("No strategy result provider is configured");
        }
        return false;
    }

    ResultTreeSummary tree;
    if (!m_provider->getTreeSummary(jobId, &tree, errorCode, errorMessage)) {
        return false;
    }

    QJsonObject summary = status.toJson();
    QJsonObject treeJson;
    treeJson.insert(QStringLiteral("rootNodeId"), tree.rootNodeId);
    treeJson.insert(QStringLiteral("nodeCount"), tree.nodeCount);
    summary.insert(QStringLiteral("tree"), treeJson);
    *result = summary;
    return true;
}

bool ResultQueryService::getNodeStrategy(const QString &jobId,
                                         const JobStatus &status,
                                         const QString &nodeId,
                                         const QStringList &actionFilter,
                                         int limit,
                                         QJsonObject *result,
                                         QString *errorCode,
                                         QString *errorMessage)
{
    return queryNode(jobId, status, nodeId, actionFilter, limit,
                     result, errorCode, errorMessage);
}

bool ResultQueryService::getActionEv(const QString &jobId,
                                     const JobStatus &status,
                                     const QString &nodeId,
                                     const QStringList &actionFilter,
                                     int limit,
                                     QJsonObject *result,
                                     QString *errorCode,
                                     QString *errorMessage)
{
    return queryNode(jobId, status, nodeId, actionFilter, limit,
                     result, errorCode, errorMessage);
}

bool ResultQueryService::getExploitability(const QString &jobId,
                                           const JobStatus &status,
                                           QJsonObject *result)
{
    if (!result) {
        return false;
    }
    QJsonObject json;
    json.insert(QStringLiteral("jobId"), jobId);
    json.insert(QStringLiteral("state"), status.state);
    json.insert(QStringLiteral("iterations"), status.iterations);
    json.insert(QStringLiteral("progress"), status.progress);
    json.insert(QStringLiteral("exploitability"), status.exploitability);
    *result = json;
    return true;
}

bool ResultQueryService::getArtifactMetadata(const QString &artifactId,
                                             QJsonObject *result,
                                             QString *errorCode,
                                             QString *errorMessage)
{
    ArtifactRef ref;
    if (!m_artifactStore->getMetadata(artifactId, &ref, errorMessage)) {
        if (errorCode) {
            *errorCode = QStringLiteral("ARTIFACT_NOT_FOUND");
        }
        return false;
    }
    if (result) {
        *result = ref.toJson();
    }
    return true;
}

bool ResultQueryService::queryNode(const QString &jobId,
                                   const JobStatus &status,
                                   const QString &nodeId,
                                   const QStringList &actionFilter,
                                   int limit,
                                   QJsonObject *result,
                                   QString *errorCode,
                                   QString *errorMessage)
{
    if (!result) {
        return false;
    }
    if (!m_provider) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_PROVIDER_UNAVAILABLE");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("No strategy result provider is configured");
        }
        return false;
    }
    if (status.state != QString::fromLatin1(JobState::Completed)) {
        if (errorCode) {
            *errorCode = QStringLiteral("RESULT_NOT_READY");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job has not completed");
        }
        return false;
    }

    NodeQueryResult nodeResult;
    if (!m_provider->queryNode(jobId, nodeId, actionFilter, limit,
                               &nodeResult, errorCode, errorMessage)) {
        return false;
    }

    ArtifactRef artifact;
    if (!m_artifactStore->store(jobId, nodeResult.artifactContent,
                                &artifact, errorMessage)) {
        if (errorCode) {
            *errorCode = QStringLiteral("ARTIFACT_WRITE_FAILED");
        }
        return false;
    }

    QJsonObject response;
    response.insert(QStringLiteral("nodeId"), nodeResult.nodeId);
    response.insert(QStringLiteral("actions"), nodeResult.actions);
    response.insert(QStringLiteral("comboCount"), nodeResult.comboCount);
    response.insert(QStringLiteral("exploitability"), status.exploitability);
    response.insert(QStringLiteral("artifactRef"), artifact.uri);
    response.insert(QStringLiteral("artifactId"), artifact.artifactId);
    *result = response;
    return true;
}

} // namespace GtoApi