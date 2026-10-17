#include "job_status.h"

namespace GtoApi {

bool JobStatus::isTerminal() const
{
    return state == QString::fromLatin1(JobState::Completed)
           || state == QString::fromLatin1(JobState::Failed)
           || state == QString::fromLatin1(JobState::Cancelled);
}

QJsonObject JobStatus::toJson() const
{
    QJsonObject json = toStateJson();
    json.insert(QStringLiteral("elapsedMs"), static_cast<double>(elapsedMs));
    return json;
}

QJsonObject JobStatus::toStateJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("jobId"), jobId);
    json.insert(QStringLiteral("state"), state);
    json.insert(QStringLiteral("progress"), progress);
    json.insert(QStringLiteral("iterations"), iterations);
    json.insert(QStringLiteral("exploitability"), exploitability);
    if (!startedAt.isEmpty()) {
        json.insert(QStringLiteral("startedAt"), startedAt);
    }
    if (!finishedAt.isEmpty()) {
        json.insert(QStringLiteral("finishedAt"), finishedAt);
    }
    if (!errorCode.isEmpty() || !errorMessage.isEmpty()) {
        QJsonObject error;
        error.insert(QStringLiteral("code"), errorCode);
        error.insert(QStringLiteral("message"), errorMessage);
        json.insert(QStringLiteral("error"), error);
    }
    return json;
}

} // namespace GtoApi