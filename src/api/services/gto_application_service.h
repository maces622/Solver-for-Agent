#ifndef GTO_API_GTO_APPLICATION_SERVICE_H
#define GTO_API_GTO_APPLICATION_SERVICE_H

#include "../dto/job_status.h"
#include "../dto/solve_options.h"
#include "../dto/spot_spec.h"
#include "../dto/state_snapshot.h"
#include "../events/event_bus.h"
#include "spot_validator.h"

#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace GtoApi {

class SolverJobManager;
class SolverJobRunner;
class StrategyResultProvider;
class ResultQueryService;
class StateStore;

enum class SetSpotStatus {
    Ok,
    Invalid,
    Conflict
};

struct SetSpotResult
{
    SetSpotStatus status = SetSpotStatus::Invalid;
    SpotSpec normalizedSpot;
    qint64 revision = 0;
    qint64 actualRevision = 0;
    SpotValidationResult validation;
};

struct StartSolveResult
{
    bool success = false;
    JobStatus status;
    qint64 actualRevision = 0;
    SpotValidationResult validation;
    QString errorCode;
    QString errorMessage;
};

struct CancelSolveResult
{
    bool success = false;
    JobStatus status;
    QString errorCode;
    QString errorMessage;
};

struct UiFocusResult
{
    bool success = false;
    QString jobId;
    QString nodeId;
    qint64 revision = 0;
    QString errorCode;
    QString errorMessage;
};

struct UiOpenResult
{
    bool success = false;
    QString jobId;
    QString errorCode;
    QString errorMessage;
};

// UI-side validation and focus seam. The API service owns state and event
// publication; a concrete GUI controller only proves the node exists and
// performs the focus/result-panel action.
class UiFocusController
{
public:
    virtual ~UiFocusController() = default;
    virtual bool focusNode(const QString &jobId, const QString &nodeId,
                           QString *errorCode = nullptr,
                           QString *errorMessage = nullptr) = 0;
    virtual bool openResult(const QString &jobId,
                            QString *errorCode = nullptr,
                            QString *errorMessage = nullptr) = 0;
};

class GtoApplicationService : public QObject
{
    Q_OBJECT

public:
    explicit GtoApplicationService(QObject *parent = nullptr);

    StateSnapshot getState() const;
    QString sessionId() const;
    QStringList capabilities() const;
    QJsonObject getCapabilities() const;
    EventBus *eventBus() const;
    SolverJobManager *jobManager() const;
    void setSolverJobRunner(SolverJobRunner *runner);
    void setStrategyResultProvider(StrategyResultProvider *provider);
    void setUiFocusController(UiFocusController *controller);
    ResultQueryService *resultQueryService() const;
    SpotValidationResult validateSpot(const SpotSpec &spot) const;
    RangeValidationResult validateRange(const QString &range,
                                         const QStringList &board,
                                         const QString &mode) const;
    SetSpotResult setSpot(const SpotSpec &spot, qint64 expectedRevision);
    StartSolveResult startSolve(const SolveOptions &options, qint64 expectedRevision);
    JobStatus getJobStatus(const QString &jobId, bool *found = nullptr) const;
    CancelSolveResult cancelSolve(const QString &jobId);
    bool getSolveSummary(const QString &jobId, QJsonObject *result,
                         QString *errorCode = nullptr,
                         QString *errorMessage = nullptr);
    bool getNodeStrategy(const QString &jobId, const QString &nodeId,
                         const QStringList &actions, int limit,
                         QJsonObject *result,
                         QString *errorCode = nullptr,
                         QString *errorMessage = nullptr);
    bool getActionEv(const QString &jobId, const QString &nodeId,
                     const QStringList &actions, int limit,
                     QJsonObject *result,
                     QString *errorCode = nullptr,
                     QString *errorMessage = nullptr);
    bool getExploitability(const QString &jobId, QJsonObject *result,
                           QString *errorCode = nullptr,
                           QString *errorMessage = nullptr);
    bool getArtifactMetadata(const QString &artifactId, QJsonObject *result,
                             QString *errorCode = nullptr,
                             QString *errorMessage = nullptr);
    UiFocusResult focusNode(const QString &jobId, const QString &nodeId);
    UiOpenResult openResult(const QString &jobId);

    bool updateFromGui(const SpotSpec &spot,
                       const QJsonValue &tree,
                       const QJsonValue &solve,
                       const QJsonValue &selection);

signals:
    void stateChanged(qint64 revision, const QStringList &changedPaths);
    void spotChanged(const SpotSpec &spot);
    void uiFocusNodeRequested(const QString &jobId, const QString &nodeId);
    void uiOpenResultRequested(const QString &jobId);

private:
    void publishStateForJob(const JobStatus &status);

    StateStore *m_stateStore = nullptr;
    SolverJobManager *m_jobManager = nullptr;
    ResultQueryService *m_resultQueryService = nullptr;
    EventBus *m_eventBus = nullptr;
    UiFocusController *m_uiFocusController = nullptr;
};

} // namespace GtoApi

#endif // GTO_API_GTO_APPLICATION_SERVICE_H