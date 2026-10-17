#include "acp_client.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

namespace RangePilot {

namespace {

constexpr int ProtocolVersion = 1;
constexpr int MaxStderrTail = 16384;

QString jsonIdKey(const QJsonValue &id)
{
    if (id.isDouble()) {
        return QString::number(static_cast<qint64>(id.toDouble()));
    }
    return id.toString();
}

} // namespace

AcpClient::AcpClient(QObject *parent)
    : QObject(parent)
{
}

AcpClient::~AcpClient()
{
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

bool AcpClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool AcpClient::isBusy() const
{
    return m_busy;
}

QString AcpClient::sessionId() const
{
    return m_sessionId;
}

void AcpClient::start(const AcpClientConfig &config)
{
    if (isRunning()) {
        return;
    }
    if (config.executable.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("DSH executable is not configured"));
        return;
    }

    m_stdoutBuffer.clear();
    m_stderrTail.clear();
    m_sessionId.clear();
    m_initialized = false;
    setBusy(false);
    m_stopping = false;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProgram(config.executable);
    m_process->setArguments(config.arguments);
    if (!config.workingDirectory.trimmed().isEmpty()) {
        m_process->setWorkingDirectory(config.workingDirectory);
    }
    m_process->setProcessEnvironment(config.environment);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &AcpClient::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &AcpClient::onReadyReadStandardError);
    connect(m_process, &QProcess::errorOccurred,
            this, &AcpClient::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AcpClient::onProcessFinished);
    connect(m_process, &QProcess::started, this, [this]() {
        sendInitialize();
    });

    emit diagnostic(QStringLiteral("Starting DSH ACP: %1 %2")
                        .arg(config.executable, config.arguments.join(QLatin1Char(' '))));
    m_process->start();
}

void AcpClient::stop()
{
    if (!isRunning()) {
        return;
    }
    m_stopping = true;
    if (!m_sessionId.isEmpty()) {
        QJsonObject params;
        params.insert(QStringLiteral("sessionId"), m_sessionId);
        sendRequest(QStringLiteral("session/close"), params,
                    [](const QJsonObject &, const QJsonObject &) {});
    }
    QPointer<QProcess> process(m_process);
    QTimer::singleShot(1200, this, [process]() {
        if (process && process->state() != QProcess::NotRunning) {
            QProcess *child = process;
            child->terminate();
            QTimer::singleShot(800, child, [process]() {
                if (process && process->state() != QProcess::NotRunning) {
                    process->kill();
                }
            });
        }
    });
}

void AcpClient::prompt(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (!m_initialized || m_sessionId.isEmpty() || !isRunning()) {
        emit errorOccurred(QStringLiteral("DSH ACP session is not ready"));
        return;
    }
    if (m_busy) {
        emit errorOccurred(QStringLiteral("A prompt is already running"));
        return;
    }

    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);

    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    params.insert(QStringLiteral("prompt"), QJsonArray{content});

    setBusy(true);
    sendRequest(QStringLiteral("session/prompt"), params,
                [this](const QJsonObject &result, const QJsonObject &error) {
        setBusy(false);
        if (!error.isEmpty()) {
            emit errorOccurred(error.value(QStringLiteral("message"))
                                   .toString(QStringLiteral("DSH prompt failed")));
            emit promptFinished(QStringLiteral("error"));
            return;
        }
        emit promptFinished(result.value(QStringLiteral("stopReason"))
                                .toString(QStringLiteral("end_turn")));
    });
}

void AcpClient::cancelPrompt()
{
    if (!m_initialized || m_sessionId.isEmpty() || !isRunning() || !m_busy) {
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    sendNotification(QStringLiteral("session/cancel"), params);
}

void AcpClient::respondPermission(const QString &requestKey,
                                  const QString &optionId,
                                  bool cancelled)
{
    const auto it = m_serverRequests.constFind(requestKey);
    if (it == m_serverRequests.constEnd()) {
        return;
    }
    QJsonObject outcome;
    if (cancelled || optionId.isEmpty()) {
        outcome.insert(QStringLiteral("outcome"), QStringLiteral("cancelled"));
    } else {
        outcome.insert(QStringLiteral("outcome"), QStringLiteral("selected"));
        outcome.insert(QStringLiteral("optionId"), optionId);
    }
    QJsonObject result;
    result.insert(QStringLiteral("outcome"), outcome);
    sendResponse(it.value(), result);
    m_serverRequests.erase(m_serverRequests.find(requestKey));
}

void AcpClient::onReadyReadStandardOutput()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    while (true) {
        const int newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit diagnostic(QStringLiteral("Ignored invalid ACP output: %1")
                                .arg(parseError.errorString()));
            continue;
        }
        handleMessage(document.object());
    }
}

void AcpClient::onReadyReadStandardError()
{
    if (!m_process) {
        return;
    }
    const QString text = QString::fromUtf8(m_process->readAllStandardError());
    if (text.isEmpty()) {
        return;
    }
    m_stderrTail.append(text);
    if (m_stderrTail.size() > MaxStderrTail) {
        m_stderrTail = m_stderrTail.right(MaxStderrTail);
    }
    emit diagnostic(text);
}

void AcpClient::onProcessError(QProcess::ProcessError error)
{
    if (!m_process) {
        return;
    }
    if (error == QProcess::FailedToStart) {
        emit errorOccurred(QStringLiteral("Failed to start DSH: %1")
                               .arg(m_process->errorString()));
    }
}

void AcpClient::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    const bool expected = m_stopping;
    if (m_process) {
        m_stderrTail.append(QString::fromUtf8(m_process->readAllStandardError()));
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_initialized = false;
    m_sessionId.clear();
    m_pendingRequests.clear();
    m_serverRequests.clear();
    setBusy(false);
    emit processStopped(exitCode, m_stderrTail);
    if (!expected) {
        emit errorOccurred(QStringLiteral("DSH ACP process exited with code %1").arg(exitCode));
    }
}

void AcpClient::handleMessage(const QJsonObject &message)
{
    if (message.contains(QStringLiteral("method"))) {
        if (message.contains(QStringLiteral("id"))) {
            handleServerRequest(message);
        } else if (message.value(QStringLiteral("method")).toString()
                   == QStringLiteral("session/update")) {
            handleSessionUpdate(message.value(QStringLiteral("params")).toObject());
        }
        return;
    }

    const QJsonValue idValue = message.value(QStringLiteral("id"));
    const QString key = jsonIdKey(idValue);
    const auto it = m_pendingRequests.find(key);
    if (it == m_pendingRequests.end()) {
        return;
    }
    const ResponseHandler handler = it.value();
    m_pendingRequests.erase(it);

    const QJsonObject error = message.value(QStringLiteral("error")).toObject();
    if (!error.isEmpty()) {
        handler(QJsonObject(), error);
        return;
    }
    handler(message.value(QStringLiteral("result")).toObject(), QJsonObject());
}

void AcpClient::handleSessionUpdate(const QJsonObject &params)
{
    const QString updateSession = params.value(QStringLiteral("sessionId")).toString();
    if (!m_sessionId.isEmpty() && !updateSession.isEmpty()
        && updateSession != m_sessionId) {
        return;
    }
    const QJsonObject update = params.value(QStringLiteral("update")).toObject();
    const QString type = update.value(QStringLiteral("sessionUpdate")).toString();
    if (type == QStringLiteral("agent_message_chunk")) {
        const QJsonObject content = update.value(QStringLiteral("content")).toObject();
        if (content.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            emit assistantText(content.value(QStringLiteral("text")).toString());
        }
        return;
    }
    if (type == QStringLiteral("agent_thought_chunk")) {
        const QJsonObject content = update.value(QStringLiteral("content")).toObject();
        if (content.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            emit agentThought(content.value(QStringLiteral("text")).toString());
        }
        return;
    }
    if (type == QStringLiteral("tool_call")
        || type == QStringLiteral("tool_call_update")) {
        emit toolStatusChanged(
            update.value(QStringLiteral("toolCallId")).toString(),
            update.value(QStringLiteral("title")).toString(),
            update.value(QStringLiteral("status")).toString());
    }
}

void AcpClient::handleServerRequest(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));
    if (method != QStringLiteral("session/request_permission")) {
        sendErrorResponse(id, -32601, QStringLiteral("Unsupported client method"));
        return;
    }

    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    const QJsonObject toolCall = params.value(QStringLiteral("toolCall")).toObject();
    const QString key = QStringLiteral("permission-%1").arg(m_nextServerRequestKey++);
    m_serverRequests.insert(key, id);
    emit permissionRequested(
        key,
        toolCall.value(QStringLiteral("title")).toString(
            QStringLiteral("GTO solve approval")),
        params.value(QStringLiteral("options")).toArray());
}

void AcpClient::sendInitialize()
{
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("RangePilotQt"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));

    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), ProtocolVersion);
    params.insert(QStringLiteral("clientCapabilities"), QJsonObject());
    params.insert(QStringLiteral("clientInfo"), clientInfo);
    sendRequest(QStringLiteral("initialize"), params,
                [this](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty()) {
            emit errorOccurred(error.value(QStringLiteral("message"))
                                   .toString(QStringLiteral("ACP initialize failed")));
            return;
        }
        const int protocol = result.value(QStringLiteral("protocolVersion")).toInt();
        if (protocol != ProtocolVersion) {
            emit errorOccurred(QStringLiteral("Unsupported ACP protocol version: %1").arg(protocol));
            return;
        }
        m_initialized = true;
        emit initialized();
        createSession();
    });
}

void AcpClient::createSession()
{
    QJsonObject params;
    params.insert(QStringLiteral("cwd"), QDir::currentPath());
    params.insert(QStringLiteral("mcpServers"), QJsonArray());
    sendRequest(QStringLiteral("session/new"), params,
                [this](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty()) {
            emit errorOccurred(error.value(QStringLiteral("message"))
                                   .toString(QStringLiteral("ACP session/new failed")));
            return;
        }
        m_sessionId = result.value(QStringLiteral("sessionId")).toString();
        if (m_sessionId.isEmpty()) {
            emit errorOccurred(QStringLiteral("ACP session/new returned no sessionId"));
            return;
        }
        emit sessionReady(m_sessionId);
    });
}

void AcpClient::sendRequest(const QString &method,
                            const QJsonObject &params,
                            ResponseHandler handler)
{
    if (!isRunning()) {
        if (handler) {
            QJsonObject error;
            error.insert(QStringLiteral("message"), QStringLiteral("DSH ACP is not running"));
            handler(QJsonObject(), error);
        }
        return;
    }
    const qint64 id = m_nextRequestId++;
    m_pendingRequests.insert(QString::number(id), std::move(handler));
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    sendMessage(message);
}

void AcpClient::sendNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    sendMessage(message);
}

void AcpClient::sendResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("result"), result);
    sendMessage(message);
}

void AcpClient::sendErrorResponse(const QJsonValue &id, int code,
                                  const QString &messageText)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), messageText);
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("error"), error);
    sendMessage(message);
}

void AcpClient::sendMessage(const QJsonObject &message)
{
    if (!isRunning()) {
        return;
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process->write(payload);
    m_process->write("\n");
    m_process->waitForBytesWritten(1);
}

void AcpClient::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged(m_busy);
}

} // namespace RangePilot
