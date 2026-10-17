#ifndef GTO_API_RESULT_QUERY_SERVICE_H
#define GTO_API_RESULT_QUERY_SERVICE_H

#include "../dto/artifact_ref.h"
#include "../dto/job_status.h"

#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace GtoApi {

class ArtifactStore;
class StrategyResultProvider;

class ResultQueryService : public QObject
{
    Q_OBJECT

public:
    explicit ResultQueryService(QObject *parent = nullptr);

    void setProvider(StrategyResultProvider *provider);
    StrategyResultProvider *provider() const;
    ArtifactStore *artifactStore() const;

    bool getSummary(const QString &jobId,
                    const JobStatus &status,
                    QJsonObject *result,
                    QString *errorCode = nullptr,
                    QString *errorMessage = nullptr);
    bool getNodeStrategy(const QString &jobId,
                         const JobStatus &status,
                         const QString &nodeId,
                         const QStringList &actionFilter,
                         int limit,
                         QJsonObject *result,
                         QString *errorCode = nullptr,
                         QString *errorMessage = nullptr);
    bool getActionEv(const QString &jobId,
                     const JobStatus &status,
                     const QString &nodeId,
                     const QStringList &actionFilter,
                     int limit,
                     QJsonObject *result,
                     QString *errorCode = nullptr,
                     QString *errorMessage = nullptr);
    bool getExploitability(const QString &jobId,
                           const JobStatus &status,
                           QJsonObject *result);
    bool getArtifactMetadata(const QString &artifactId,
                             QJsonObject *result,
                             QString *errorCode = nullptr,
                             QString *errorMessage = nullptr);

private:
    bool queryNode(const QString &jobId,
                   const JobStatus &status,
                   const QString &nodeId,
                   const QStringList &actionFilter,
                   int limit,
                   QJsonObject *result,
                   QString *errorCode,
                   QString *errorMessage);

    StrategyResultProvider *m_provider = nullptr;
    ArtifactStore *m_artifactStore = nullptr;
};

} // namespace GtoApi

#endif // GTO_API_RESULT_QUERY_SERVICE_H