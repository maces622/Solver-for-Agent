#include "gto_application_service.h"

#include "../protocol.h"
#include "solver_job_manager.h"
#include "result_query_service.h"
#include "solver_job_runner.h"
#include "strategy_result_provider.h"
#include "state_store.h"

#include <QJsonArray>
#include <QUuid>

namespace GtoApi {

namespace {

void appendMethod(QJsonArray &methods,
                  const QString &name,
                  const QJsonValue &capability,
                  bool async)
{
    QJsonObject method;
    method.insert(QStringLiteral("name"), name);
    method.insert(QStringLiteral("capability"), capability);
    method.insert(QStringLiteral("async"), async);
    methods.append(method);
}

} // namespace

GtoApplicationService::GtoApplicationService(QObject *parent)
    : QObject(parent)
{
    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_stateStore = new StateStore(sessionId, this);
    m_eventBus = new EventBus(this);
    m_jobManager = new SolverJobManager(this);
    m_resultQueryService = new ResultQueryService(this);

    connect(m_jobManager, &SolverJobManager::jobQueued, this,
            [this](const JobStatus &status) {
        publishStateForJob(status);
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), status.jobId);
        params.insert(QStringLiteral("state"), status.state);
        m_eventBus->publish(QStringLiteral("event.solve_queued"), params);
    });
    connect(m_jobManager, &SolverJobManager::jobProgress, this,
            [this](const JobStatus &status) {
        publishStateForJob(status);
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), status.jobId);
        params.insert(QStringLiteral("progress"), status.progress);
        params.insert(QStringLiteral("iterations"), status.iterations);
        params.insert(QStringLiteral("exploitability"), status.exploitability);
        params.insert(QStringLiteral("elapsedMs"), static_cast<double>(status.elapsedMs));
        m_eventBus->publish(QStringLiteral("event.solve_progress"), params);
    });
    connect(m_jobManager, &SolverJobManager::jobCompleted, this,
            [this](const JobStatus &status) {
        publishStateForJob(status);
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), status.jobId);
        params.insert(QStringLiteral("state"), status.state);
        params.insert(QStringLiteral("progress"), status.progress);
        params.insert(QStringLiteral("elapsedMs"), static_cast<double>(status.elapsedMs));
        m_eventBus->publish(QStringLiteral("event.solve_completed"), params);
    });
    connect(m_jobManager, &SolverJobManager::jobFailed, this,
            [this](const JobStatus &status) {
        publishStateForJob(status);
        QJsonObject error;
        error.insert(QStringLiteral("code"), status.errorCode);
        error.insert(QStringLiteral("message"), status.errorMessage);
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), status.jobId);
        params.insert(QStringLiteral("state"), status.state);
        params.insert(QStringLiteral("error"), error);
        m_eventBus->publish(QStringLiteral("event.solve_failed"), params);
    });
    connect(m_jobManager, &SolverJobManager::jobCancelled, this,
            [this](const JobStatus &status) {
        publishStateForJob(status);
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), status.jobId);
        params.insert(QStringLiteral("state"), status.state);
        m_eventBus->publish(QStringLiteral("event.solve_cancelled"), params);
    });

    connect(m_stateStore, &StateStore::stateChanged, this,
            [this](qint64 revision, const QStringList &changedPaths) {
        emit stateChanged(revision, changedPaths);

        QJsonObject params;
        params.insert(QStringLiteral("revision"), static_cast<double>(revision));
        params.insert(QStringLiteral("changedPaths"), QJsonArray::fromStringList(changedPaths));
        m_eventBus->publish(QStringLiteral("event.state_changed"), params);
    });
}

StateSnapshot GtoApplicationService::getState() const
{
    return m_stateStore->snapshot();
}

QString GtoApplicationService::sessionId() const
{
    return m_stateStore->sessionId();
}

QStringList GtoApplicationService::capabilities() const
{
    return {QStringLiteral("state.read"), QStringLiteral("spot.write"), QStringLiteral("solve.control"), QStringLiteral("result.read"), QStringLiteral("artifact.read")};
}

QJsonObject GtoApplicationService::getCapabilities() const
{
    QJsonArray capabilityArray;
    for (const QString &capability : capabilities()) {
        capabilityArray.append(capability);
    }

    QJsonArray methods;
    appendMethod(methods, QStringLiteral("system.hello"), QJsonValue::Null, false);
    appendMethod(methods, QStringLiteral("system.getCapabilities"), QJsonValue::Null, false);
    appendMethod(methods, QStringLiteral("system.health"), QJsonValue::Null, false);
    appendMethod(methods, QStringLiteral("state.get"), QStringLiteral("state.read"), false);
    appendMethod(methods, QStringLiteral("spot.validate"), QStringLiteral("spot.write"), false);
    appendMethod(methods, QStringLiteral("range.validate"), QStringLiteral("spot.write"), false);
    appendMethod(methods, QStringLiteral("spot.set"), QStringLiteral("spot.write"), false);
    appendMethod(methods, QStringLiteral("solve.start"), QStringLiteral("solve.control"), true);
    appendMethod(methods, QStringLiteral("solve.getStatus"), QStringLiteral("solve.control"), false);
    appendMethod(methods, QStringLiteral("solve.cancel"), QStringLiteral("solve.control"), false);
    appendMethod(methods, QStringLiteral("solve.getSummary"), QStringLiteral("result.read"), false);
    appendMethod(methods, QStringLiteral("result.getNodeStrategy"), QStringLiteral("result.read"), false);
    appendMethod(methods, QStringLiteral("result.getActionEv"), QStringLiteral("result.read"), false);
    appendMethod(methods, QStringLiteral("result.getExploitability"), QStringLiteral("result.read"), false);
    appendMethod(methods, QStringLiteral("artifact.getMetadata"), QStringLiteral("artifact.read"), false);

    QJsonObject limits;
    limits.insert(QStringLiteral("maxMessageBytes"), MaxMessageBytes);

    QJsonObject result;
    result.insert(QStringLiteral("protocol"), QString::fromLatin1(ProtocolVersion));
    result.insert(QStringLiteral("capabilities"), capabilityArray);
    result.insert(QStringLiteral("methods"), methods);
    result.insert(QStringLiteral("limits"), limits);
    return result;
}

EventBus *GtoApplicationService::eventBus() const
{
    return m_eventBus;
}

SpotValidationResult GtoApplicationService::validateSpot(const SpotSpec &spot) const
{
    return SpotValidator::validate(spot);
}

RangeValidationResult GtoApplicationService::validateRange(const QString &range,
                                                           const QStringList &board,
                                                           const QString &mode) const
{
    return SpotValidator::validateRange(range, board, mode);
}

SetSpotResult GtoApplicationService::setSpot(const SpotSpec &spot,
                                             qint64 expectedRevision)
{
    SetSpotResult result;
    result.validation = SpotValidator::validate(spot);
    result.actualRevision = m_stateStore->revision();
    if (!result.validation.valid) {
        result.status = SetSpotStatus::Invalid;
        return result;
    }
    if (expectedRevision != result.actualRevision) {
        result.status = SetSpotStatus::Conflict;
        return result;
    }

    result.normalizedSpot = result.validation.normalizedSpot;
    const bool changed = m_stateStore->setSpot(result.normalizedSpot);
    result.revision = m_stateStore->revision();
    result.status = SetSpotStatus::Ok;
    if (changed) {
        emit spotChanged(result.normalizedSpot);
    }
    return result;
}
SolverJobManager *GtoApplicationService::jobManager() const
{
    return m_jobManager;
}

void GtoApplicationService::setSolverJobRunner(SolverJobRunner *runner)
{
    m_jobManager->setRunner(runner);
}

StartSolveResult GtoApplicationService::startSolve(const SolveOptions &options,
                                                  qint64 expectedRevision)
{
    StartSolveResult result;
    const StateSnapshot snapshot = m_stateStore->snapshot();
    result.actualRevision = snapshot.revision;
    result.validation = SpotValidator::validate(snapshot.spot);
    if (!result.validation.valid) {
        result.errorCode = QStringLiteral("SPOT_INVALID");
        result.errorMessage = QStringLiteral("Current spot is not valid");
        return result;
    }
    if (expectedRevision != snapshot.revision) {
        result.errorCode = QStringLiteral("STATE_CONFLICT");
        result.errorMessage = QStringLiteral("State revision conflict");
        return result;
    }

    const JobStartResult started = m_jobManager->start(snapshot.spot, options);
    if (!started.success) {
        result.errorCode = started.errorCode;
        result.errorMessage = started.errorMessage;
        result.status = started.status;
        return result;
    }

    result.success = true;
    result.status = started.status;
    return result;
}

JobStatus GtoApplicationService::getJobStatus(const QString &jobId, bool *found) const
{
    return m_jobManager->getStatus(jobId, found);
}

CancelSolveResult GtoApplicationService::cancelSolve(const QString &jobId)
{
    CancelSolveResult result;
    bool ok = false;
    result.status = m_jobManager->cancel(jobId, &ok,
                                         &result.errorCode,
                                         &result.errorMessage);
    result.success = ok;
    return result;
}

void GtoApplicationService::publishStateForJob(const JobStatus &status)
{
    const StateSnapshot snapshot = m_stateStore->snapshot();
    m_stateStore->updateFromGui(snapshot.spot,
                                snapshot.tree,
                                status.toStateJson(),
                                snapshot.selection);
}
void GtoApplicationService::setStrategyResultProvider(StrategyResultProvider *provider)
{
    m_resultQueryService->setProvider(provider);
}

ResultQueryService *GtoApplicationService::resultQueryService() const
{
    return m_resultQueryService;
}

bool GtoApplicationService::getSolveSummary(const QString &jobId,
                                            QJsonObject *result,
                                            QString *errorCode,
                                            QString *errorMessage)
{
    bool found = false;
    const JobStatus status = m_jobManager->getStatus(jobId, &found);
    if (!found) {
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_FOUND");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job was not found");
        }
        return false;
    }
    return m_resultQueryService->getSummary(jobId, status, result,
                                            errorCode, errorMessage);
}

bool GtoApplicationService::getNodeStrategy(const QString &jobId,
                                            const QString &nodeId,
                                            const QStringList &actions,
                                            int limit,
                                            QJsonObject *result,
                                            QString *errorCode,
                                            QString *errorMessage)
{
    bool found = false;
    const JobStatus status = m_jobManager->getStatus(jobId, &found);
    if (!found) {
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_FOUND");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job was not found");
        }
        return false;
    }
    return m_resultQueryService->getNodeStrategy(jobId, status, nodeId, actions,
                                                 limit, result,
                                                 errorCode, errorMessage);
}

bool GtoApplicationService::getActionEv(const QString &jobId,
                                        const QString &nodeId,
                                        const QStringList &actions,
                                        int limit,
                                        QJsonObject *result,
                                        QString *errorCode,
                                        QString *errorMessage)
{
    bool found = false;
    const JobStatus status = m_jobManager->getStatus(jobId, &found);
    if (!found) {
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_FOUND");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job was not found");
        }
        return false;
    }
    return m_resultQueryService->getActionEv(jobId, status, nodeId, actions,
                                             limit, result,
                                             errorCode, errorMessage);
}

bool GtoApplicationService::getExploitability(const QString &jobId,
                                              QJsonObject *result,
                                              QString *errorCode,
                                              QString *errorMessage)
{
    bool found = false;
    const JobStatus status = m_jobManager->getStatus(jobId, &found);
    if (!found) {
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_FOUND");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job was not found");
        }
        return false;
    }
    return m_resultQueryService->getExploitability(jobId, status, result);
}

bool GtoApplicationService::getArtifactMetadata(const QString &artifactId,
                                                QJsonObject *result,
                                                QString *errorCode,
                                                QString *errorMessage)
{
    return m_resultQueryService->getArtifactMetadata(artifactId, result,
                                                     errorCode, errorMessage);
}
bool GtoApplicationService::updateFromGui(const SpotSpec &spot,
                                          const QJsonValue &tree,
                                          const QJsonValue &solve,
                                          const QJsonValue &selection)
{
    return m_stateStore->updateFromGui(spot, tree, solve, selection);
}

} // namespace GtoApi