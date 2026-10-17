#ifndef GTO_API_SOLVER_JOB_MANAGER_H
#define GTO_API_SOLVER_JOB_MANAGER_H

#include "../dto/job_status.h"
#include "../dto/solve_options.h"
#include "../dto/spot_spec.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>

namespace GtoApi {

class SolverJobRunner;

struct JobStartResult
{
    bool success = false;
    QString jobId;
    JobStatus status;
    QString errorCode;
    QString errorMessage;
};

class SolverJobManager : public QObject
{
    Q_OBJECT

public:
    explicit SolverJobManager(QObject *parent = nullptr);

    void setRunner(SolverJobRunner *runner);
    SolverJobRunner *runner() const;

    JobStartResult start(const SpotSpec &spot, const SolveOptions &options);
    JobStatus getStatus(const QString &jobId, bool *found = nullptr) const;
    JobStatus cancel(const QString &jobId,
                     bool *success = nullptr,
                     QString *errorCode = nullptr,
                     QString *errorMessage = nullptr);

    QString activeJobId() const;
    bool hasActiveJob() const;

signals:
    void jobQueued(const JobStatus &status);
    void jobProgress(const JobStatus &status);
    void jobCompleted(const JobStatus &status);
    void jobFailed(const JobStatus &status);
    void jobCancelled(const JobStatus &status);

private slots:
    void onRunnerProgress(int iteration, int maxIterations, double exploitability);
    void onRunnerCompleted();
    void onRunnerFailed(const QString &message);
    void onRunnerCancelled();

private:
    JobStatus &activeStatus();
    void finishJob(const QString &state);

    SolverJobRunner *m_runner = nullptr;
    QHash<QString, JobStatus> m_jobs;
    QString m_activeJobId;
    QElapsedTimer m_elapsed;
};

} // namespace GtoApi

#endif // GTO_API_SOLVER_JOB_MANAGER_H