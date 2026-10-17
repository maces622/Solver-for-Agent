#ifndef GTO_API_JOB_STATUS_H
#define GTO_API_JOB_STATUS_H

#include <QJsonObject>
#include <QString>

namespace GtoApi {

namespace JobState {
constexpr const char *Queued = "queued";
constexpr const char *Running = "running";
constexpr const char *Completed = "completed";
constexpr const char *Failed = "failed";
constexpr const char *Cancelling = "cancelling";
constexpr const char *Cancelled = "cancelled";
} // namespace JobState

struct JobStatus
{
    QString jobId;
    QString state = QString::fromLatin1(JobState::Queued);
    double progress = 0.0;
    int iterations = 0;
    double exploitability = 0.0;
    QString startedAt;
    QString finishedAt;
    qint64 elapsedMs = 0;
    QString errorCode;
    QString errorMessage;

    bool isTerminal() const;
    QJsonObject toJson() const;
    QJsonObject toStateJson() const;
};

} // namespace GtoApi

#endif // GTO_API_JOB_STATUS_H