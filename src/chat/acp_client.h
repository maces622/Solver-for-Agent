#ifndef RANGEPILOT_ACP_CLIENT_H
#define RANGEPILOT_ACP_CLIENT_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

namespace RangePilot {

struct AcpClientConfig
{
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};

// Minimal ACP v1 client over newline-delimited JSON-RPC on a child process's
// stdin/stdout. stderr is treated as diagnostics and never as protocol input.
class AcpClient : public QObject
{
    Q_OBJECT

public:
    explicit AcpClient(QObject *parent = nullptr);
    ~AcpClient() override;

    void start(const AcpClientConfig &config);
    void stop();
    bool isRunning() const;
    bool isBusy() const;
    QString sessionId() const;

    void prompt(const QString &text);
    void cancelPrompt();
    void respondPermission(const QString &requestKey,
                           const QString &optionId,
                           bool cancelled = false);

signals:
    void initialized();
    void sessionReady(const QString &sessionId);
    void assistantText(const QString &text);
    void agentThought(const QString &text);
    void toolStatusChanged(const QString &toolCallId,
                           const QString &title,
                           const QString &status);
    void permissionRequested(const QString &requestKey,
                             const QString &title,
                             const QJsonArray &options);
    void promptFinished(const QString &stopReason);
    void busyChanged(bool busy);
    void errorOccurred(const QString &message);
    void processStopped(int exitCode, const QString &stderrTail);
    void diagnostic(const QString &message);

private:
    using ResponseHandler = std::function<void(const QJsonObject &result,
                                               const QJsonObject &error)>;

    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void handleMessage(const QJsonObject &message);
    void handleSessionUpdate(const QJsonObject &params);
    void handleServerRequest(const QJsonObject &message);
    void sendInitialize();
    void createSession();
    void sendRequest(const QString &method,
                     const QJsonObject &params,
                     ResponseHandler handler);
    void sendNotification(const QString &method, const QJsonObject &params);
    void sendResponse(const QJsonValue &id, const QJsonObject &result);
    void sendErrorResponse(const QJsonValue &id, int code,
                           const QString &message);
    void sendMessage(const QJsonObject &message);
    void setBusy(bool busy);

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QString m_stderrTail;
    QString m_sessionId;
    QHash<QString, ResponseHandler> m_pendingRequests;
    QHash<QString, QJsonValue> m_serverRequests;
    qint64 m_nextRequestId = 1;
    qint64 m_nextServerRequestKey = 1;
    bool m_initialized = false;
    bool m_busy = false;
    bool m_stopping = false;
};

} // namespace RangePilot

#endif // RANGEPILOT_ACP_CLIENT_H
