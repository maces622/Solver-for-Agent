#include "solver_job_manager.h"

#include "solver_job_runner.h"

#include <QDateTime>
#include <QUuid>

namespace GtoApi {

SolverJobManager::SolverJobManager(QObject *parent)
    : QObject(parent)
{
}

void SolverJobManager::setRunner(SolverJobRunner *runner)
{
    if (m_runner == runner) {
        return;
    }
    if (m_runner) {
        disconnect(m_runner, nullptr, this, nullptr);
    }
    m_runner = runner;
    if (m_runner) {
        connect(m_runner, &SolverJobRunner::progress,
                this, &SolverJobManager::onRunnerProgress);
        connect(m_runner, &SolverJobRunner::completed,
                this, &SolverJobManager::onRunnerCompleted);
        connect(m_runner, &SolverJobRunner::failed,
                this, &SolverJobManager::onRunnerFailed);
        connect(m_runner, &SolverJobRunner::cancelled,
                this, &SolverJobManager::onRunnerCancelled);
    }
}

SolverJobRunner *SolverJobManager::runner() const
{
    return m_runner;
}

JobStartResult SolverJobManager::start(const SpotSpec &spot, const SolveOptions &options)
{
    JobStartResult result;
    if (!m_runner) {
        result.errorCode = QStringLiteral("SOLVER_UNAVAILABLE");
        result.errorMessage = QStringLiteral("No solver job runner is configured");
        return result;
    }
    if (hasActiveJob()) {
        result.errorCode = QStringLiteral("SOLVER_BUSY");
        result.errorMessage = QStringLiteral("Another solve job is already active");
        return result;
    }

    JobStatus status;
    status.jobId = QStringLiteral("job-")
                   + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    status.state = QString::fromLatin1(JobState::Queued);
    status.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_jobs.insert(status.jobId, status);
    m_activeJobId = status.jobId;

    const JobStatus queuedStatus = status;
    emit jobQueued(queuedStatus);

    QString startError;
    m_elapsed.start();
    if (!m_runner->start(spot, options, &startError)) {
        JobStatus &failed = activeStatus();
        failed.state = QString::fromLatin1(JobState::Failed);
        failed.finishedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        failed.elapsedMs = m_elapsed.elapsed();
        failed.errorCode = QStringLiteral("SOLVER_START_FAILED");
        failed.errorMessage = startError.isEmpty()
                                  ? QStringLiteral("Solver failed to start")
                                  : startError;
        const JobStatus failedStatus = failed;
        m_activeJobId.clear();
        emit jobFailed(failedStatus);

        result.jobId = failedStatus.jobId;
        result.status = failedStatus;
        result.errorCode = failedStatus.errorCode;
        result.errorMessage = failedStatus.errorMessage;
        return result;
    }

    activeStatus().state = QString::fromLatin1(JobState::Running);
    result.success = true;
    result.jobId = status.jobId;
    result.status = queuedStatus;
    return result;
}

JobStatus SolverJobManager::getStatus(const QString &jobId, bool *found) const
{
    auto it = m_jobs.constFind(jobId);
    if (it == m_jobs.constEnd()) {
        if (found) {
            *found = false;
        }
        return JobStatus();
    }
    if (found) {
        *found = true;
    }
    return it.value();
}

JobStatus SolverJobManager::cancel(const QString &jobId,
                                   bool *success,
                                   QString *errorCode,
                                   QString *errorMessage)
{
    auto it = m_jobs.find(jobId);
    if (it == m_jobs.end()) {
        if (success) {
            *success = false;
        }
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_FOUND");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job was not found");
        }
        return JobStatus();
    }

    JobStatus &status = it.value();
    if (status.isTerminal()) {
        if (success) {
            *success = false;
        }
        if (errorCode) {
            *errorCode = QStringLiteral("JOB_NOT_ACTIVE");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve job is no longer active");
        }
        return status;
    }

    status.state = QString::fromLatin1(JobState::Cancelling);
    if (success) {
        *success = true;
    }
    if (m_runner) {
        m_runner->cancel();
    }
    return status;
}

QString SolverJobManager::activeJobId() const
{
    return m_activeJobId;
}

bool SolverJobManager::hasActiveJob() const
{
    if (m_activeJobId.isEmpty()) {
        return false;
    }
    const JobStatus status = m_jobs.value(m_activeJobId);
    return !status.isTerminal();
}

void SolverJobManager::onRunnerProgress(int iteration,
                                        int maxIterations,
                                        double exploitability)
{
    if (m_activeJobId.isEmpty()) {
        return;
    }
    JobStatus &status = activeStatus();
    if (status.state != QString::fromLatin1(JobState::Cancelling)) {
        status.state = QString::fromLatin1(JobState::Running);
    }
    status.iterations = iteration;
    status.progress = maxIterations > 0
                          ? qBound(0.0, static_cast<double>(iteration) / maxIterations, 1.0)
                          : 0.0;
    status.exploitability = exploitability;
    status.elapsedMs = m_elapsed.elapsed();
    emit jobProgress(status);
}

void SolverJobManager::onRunnerCompleted()
{
    if (m_activeJobId.isEmpty()) {
        return;
    }
    JobStatus &status = activeStatus();
    status.progress = 1.0;
    finishJob(QString::fromLatin1(JobState::Completed));
    const JobStatus completed = status;
    emit jobCompleted(completed);
}

void SolverJobManager::onRunnerFailed(const QString &message)
{
    if (m_activeJobId.isEmpty()) {
        return;
    }
    JobStatus &status = activeStatus();
    status.errorCode = QStringLiteral("SOLVER_ERROR");
    status.errorMessage = message.isEmpty() ? QStringLiteral("Solver failed") : message;
    finishJob(QString::fromLatin1(JobState::Failed));
    emit jobFailed(status);
}

void SolverJobManager::onRunnerCancelled()
{
    if (m_activeJobId.isEmpty()) {
        return;
    }
    JobStatus &status = activeStatus();
    finishJob(QString::fromLatin1(JobState::Cancelled));
    emit jobCancelled(status);
}

JobStatus &SolverJobManager::activeStatus()
{
    return m_jobs[m_activeJobId];
}

void SolverJobManager::finishJob(const QString &state)
{
    JobStatus &status = activeStatus();
    status.state = state;
    status.finishedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    status.elapsedMs = m_elapsed.elapsed();
    m_activeJobId.clear();
}

} // namespace GtoApi