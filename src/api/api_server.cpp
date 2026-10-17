#include "api_server.h"

#include "services/gto_application_service.h"

#include <QAbstractSocket>
#include <cmath>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketProtocol>
#include <QWebSocketServer>

namespace GtoApi {

namespace {

bool secureEquals(const QString &left, const QString &right)
{
    const QByteArray lhs = left.toUtf8();
    const QByteArray rhs = right.toUtf8();
    if (lhs.size() != rhs.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (int index = 0; index < lhs.size(); ++index) {
        difference |= static_cast<unsigned char>(lhs.at(index) ^ rhs.at(index));
    }
    return difference == 0;
}

bool parseRevision(const QJsonValue &value, qint64 *revision, QString *errorMessage)
{
    if (!value.isDouble()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("expectedRevision must be an integer");
        }
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0
        || std::floor(number) != number || number > 9007199254740991.0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("expectedRevision must be a non-negative integer");
        }
        return false;
    }
    *revision = static_cast<qint64>(number);
    return true;
}
int resultErrorRpcCode(const QString &errorCode)
{
    if (errorCode == QStringLiteral("JOB_NOT_FOUND")) {
        return ErrorCode::JobNotFound;
    }
    if (errorCode == QStringLiteral("NODE_NOT_FOUND")) {
        return ErrorCode::NodeNotFound;
    }
    if (errorCode == QStringLiteral("ARTIFACT_NOT_FOUND")) {
        return ErrorCode::ArtifactNotFound;
    }
    if (errorCode == QStringLiteral("RESULT_NOT_READY")
        || errorCode == QStringLiteral("RESULT_PROVIDER_UNAVAILABLE")
        || errorCode == QStringLiteral("RESULT_EMPTY")
        || errorCode == QStringLiteral("RESULT_QUERY_FAILED")) {
        return ErrorCode::ResultNotReady;
    }
    return ErrorCode::InvalidParams;
}

bool readStringArray(const QJsonObject &object,
                     const QString &key,
                     QStringList *values,
                     QString *errorMessage)
{
    values->clear();
    if (!object.contains(key)) {
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 must be an array of strings").arg(key);
        }
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("%1 must contain only strings").arg(key);
            }
            return false;
        }
        values->append(item.toString());
    }
    return true;
}

} // namespace

LocalApiServer::LocalApiServer(GtoApplicationService *service, QObject *parent)
    : QObject(parent)
    , m_service(service)
{
    if (m_service && m_service->eventBus()) {
        connect(m_service->eventBus(), &EventBus::eventReady, this,
                [this](const QString &method, const QJsonObject &params) {
            broadcastEvent(method, params);
        });
    }
}

LocalApiServer::~LocalApiServer()
{
    stop();
}

QString LocalApiServer::defaultDiscoveryFilePath()
{
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    }
    return QDir(base).filePath(QStringLiteral("RangePilot/gto-gui/api.json"));
}

bool LocalApiServer::start(const ApiServerConfig &config, QString *errorMessage)
{
    if (!m_service) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Application service is not available.");
        }
        return false;
    }
    if (m_server && m_server->isListening()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("API server is already running.");
        }
        return false;
    }

    m_enabledCapabilities.clear();
    for (const QString &capability : config.enabledCapabilities) {
        m_enabledCapabilities.insert(capability);
    }

    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces)
              + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_discoveryFilePath = config.discoveryFilePath.isEmpty()
                              ? defaultDiscoveryFilePath()
                              : config.discoveryFilePath;
    m_startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    m_server = new QWebSocketServer(QStringLiteral("GTO Solver Local API"),
                                    QWebSocketServer::NonSecureMode,
                                    this);
    connect(m_server, &QWebSocketServer::newConnection,
            this, &LocalApiServer::onNewConnection);

    if (!m_server->listen(QHostAddress::LocalHost, config.port)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot listen on 127.0.0.1:%1: %2")
                                .arg(config.port)
                                .arg(m_server->errorString());
        }
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_port = m_server->serverPort();
    if (!writeDiscoveryFile(errorMessage)) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        m_port = 0;
        return false;
    }

    qInfo().noquote() << QStringLiteral("GTO API listening on 127.0.0.1:%1/rpc").arg(m_port);
    return true;
}

void LocalApiServer::stop()
{
    for (QWebSocket *socket : m_clients.keys()) {
        socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                      QStringLiteral("Server shutting down"));
    }
    m_clients.clear();

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    removeDiscoveryFile();
    m_port = 0;
    m_token.clear();
}

bool LocalApiServer::isListening() const
{
    return m_server && m_server->isListening();
}

quint16 LocalApiServer::port() const
{
    return m_port;
}

QString LocalApiServer::token() const
{
    return m_token;
}

QString LocalApiServer::discoveryFilePath() const
{
    return m_discoveryFilePath;
}

void LocalApiServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        const QString resourcePath = QUrl(socket->resourceName()).path();
        if (resourcePath != QStringLiteral("/rpc")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("Unknown WebSocket resource"));
            socket->deleteLater();
            continue;
        }

        m_clients.insert(socket, ClientState());
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString &message) {
            onTextMessage(socket, message);
        });
        connect(socket, &QWebSocket::disconnected,
                this, &LocalApiServer::onSocketDisconnected);
    }
}

void LocalApiServer::onTextMessage(QWebSocket *socket, const QString &message)
{
    if (!m_clients.contains(socket)) {
        return;
    }

    if (message.toUtf8().size() > MaxMessageBytes) {
        sendError(socket, QJsonValue::Null,
                  ErrorCode::InvalidRequest,
                  QStringLiteral("Request exceeds the maximum message size"),
                  errorData(QStringLiteral("MESSAGE_TOO_LARGE")));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        sendError(socket, QJsonValue::Null, ErrorCode::ParseError,
                  QStringLiteral("Parse error"), errorData(QStringLiteral("PARSE_ERROR")));
        return;
    }
    if (!document.isObject()) {
        sendError(socket, QJsonValue::Null, ErrorCode::InvalidRequest,
                  QStringLiteral("JSON-RPC batch requests are not supported"),
                  errorData(QStringLiteral("INVALID_REQUEST")));
        return;
    }

    const QJsonObject request = document.object();
    const bool isNotification = !request.contains(QStringLiteral("id"));
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QJsonObject response = dispatch(socket, request);
    if (!isNotification && !response.isEmpty()) {
        sendJson(socket, response);
    }
}

void LocalApiServer::onSocketDisconnected()
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket) {
        return;
    }
    m_clients.remove(socket);
    socket->deleteLater();
}

QJsonObject LocalApiServer::dispatch(QWebSocket *socket, const QJsonObject &request)
{
    QJsonValue id = request.value(QStringLiteral("id"));
    if (!request.contains(QStringLiteral("id"))) {
        id = QJsonValue::Undefined;
    } else if (!id.isString() && !id.isDouble() && !id.isNull()) {
        return makeFailure(QJsonValue::Null, ErrorCode::InvalidRequest,
                           QStringLiteral("JSON-RPC id must be a string, number, or null"),
                           errorData(QStringLiteral("INVALID_REQUEST")));
    }

    if (request.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
        return makeFailure(id, ErrorCode::InvalidRequest,
                           QStringLiteral("Invalid JSON-RPC version"),
                           errorData(QStringLiteral("INVALID_REQUEST")));
    }

    const QJsonValue methodValue = request.value(QStringLiteral("method"));
    if (!methodValue.isString() || methodValue.toString().isEmpty()) {
        return makeFailure(id, ErrorCode::InvalidRequest,
                           QStringLiteral("Method must be a non-empty string"),
                           errorData(QStringLiteral("INVALID_REQUEST")));
    }

    const QString method = methodValue.toString();
    QJsonObject params;
    if (request.contains(QStringLiteral("params"))
        && !request.value(QStringLiteral("params")).isUndefined()
        && !request.value(QStringLiteral("params")).isNull()) {
        if (!request.value(QStringLiteral("params")).isObject()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("Params must be an object"),
                               errorData(QStringLiteral("INVALID_PARAMS")));
        }
        params = request.value(QStringLiteral("params")).toObject();
    }

    if (method == QStringLiteral("system.hello")) {
        const QString protocol = params.value(QStringLiteral("protocol")).toString();
        if (protocol != QString::fromLatin1(ProtocolVersion)) {
            return makeFailure(id, ErrorCode::ProtocolMismatch,
                               QStringLiteral("Unsupported protocol version"),
                               errorData(QStringLiteral("PROTOCOL_MISMATCH"),
                                         QJsonObject{{QStringLiteral("expected"),
                                                      QString::fromLatin1(ProtocolVersion)},
                                                     {QStringLiteral("actual"), protocol}}));
        }

        const QString token = params.value(QStringLiteral("token")).toString();
        if (token.isEmpty() || !secureEquals(token, m_token)) {
            return makeFailure(id, ErrorCode::Unauthorized,
                               QStringLiteral("Invalid API token"),
                               errorData(QStringLiteral("UNAUTHORIZED")));
        }

        return makeSuccess(id, handleHello(socket, params));
    }

    ClientState &client = m_clients[socket];
    if (!client.authenticated) {
        return makeFailure(id, ErrorCode::Unauthorized,
                           QStringLiteral("Call system.hello before other methods"),
                           errorData(QStringLiteral("UNAUTHORIZED")));
    }

    if (method == QStringLiteral("system.getCapabilities")) {
        return makeSuccess(id, capabilitiesResult());
    }
    if (method == QStringLiteral("system.health")) {
        const QDateTime startedAt = QDateTime::fromString(m_startedAt, Qt::ISODate);
        QJsonObject result;
        result.insert(QStringLiteral("status"), QStringLiteral("ok"));
        result.insert(QStringLiteral("protocol"), QString::fromLatin1(ProtocolVersion));
        result.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
        result.insert(QStringLiteral("uptimeSeconds"),
                      static_cast<double>(startedAt.secsTo(QDateTime::currentDateTimeUtc())));
        return makeSuccess(id, result);
    }
    if (method == QStringLiteral("state.get")) {
        if (!capabilityEnabled(QStringLiteral("state.read"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("state.read")}}));
        }
        return makeSuccess(id, m_service->getState().toJson());
    }

    if (method == QStringLiteral("spot.validate")) {
        if (!capabilityEnabled(QStringLiteral("spot.write"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("spot.write")}}));
        }
        const QJsonValue spotValue = params.value(QStringLiteral("spot"));
        if (!spotValue.isObject()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("spot must be an object"),
                               errorData(QStringLiteral("INVALID_SPOT")));
        }
        SpotSpec spot;
        QString parseError;
        if (!SpotSpec::fromJson(spotValue.toObject(), &spot, &parseError)) {
            QJsonObject data = errorData(QStringLiteral("INVALID_SPOT"));
            data.insert(QStringLiteral("detail"), parseError);
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("Invalid spot payload"), data);
        }
        return makeSuccess(id, m_service->validateSpot(spot).toJson());
    }

    if (method == QStringLiteral("range.validate")) {
        if (!capabilityEnabled(QStringLiteral("spot.write"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("spot.write")}}));
        }
        const QJsonValue rangeValue = params.value(QStringLiteral("range"));
        if (!rangeValue.isString()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("range must be a string"),
                               errorData(QStringLiteral("INVALID_RANGE")));
        }

        QStringList board;
        if (params.contains(QStringLiteral("board"))) {
            const QJsonValue boardValue = params.value(QStringLiteral("board"));
            if (!boardValue.isArray()) {
                return makeFailure(id, ErrorCode::InvalidParams,
                                   QStringLiteral("board must be an array of strings"),
                                   errorData(QStringLiteral("INVALID_BOARD")));
            }
            for (const QJsonValue &card : boardValue.toArray()) {
                if (!card.isString()) {
                    return makeFailure(id, ErrorCode::InvalidParams,
                                       QStringLiteral("board must contain only strings"),
                                       errorData(QStringLiteral("INVALID_BOARD")));
                }
                board.append(card.toString());
            }
        }

        QString mode = QStringLiteral("holdem");
        if (params.contains(QStringLiteral("mode"))) {
            const QJsonValue modeValue = params.value(QStringLiteral("mode"));
            if (!modeValue.isString()) {
                return makeFailure(id, ErrorCode::InvalidParams,
                                   QStringLiteral("mode must be a string"),
                                   errorData(QStringLiteral("INVALID_MODE")));
            }
            mode = modeValue.toString();
        }

        return makeSuccess(id, m_service->validateRange(rangeValue.toString(), board, mode).toJson());
    }

    if (method == QStringLiteral("spot.set")) {
        if (!capabilityEnabled(QStringLiteral("spot.write"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("spot.write")}}));
        }

        qint64 expectedRevision = 0;
        QString revisionError;
        if (!parseRevision(params.value(QStringLiteral("expectedRevision")),
                           &expectedRevision, &revisionError)) {
            return makeFailure(id, ErrorCode::InvalidParams, revisionError,
                               errorData(QStringLiteral("INVALID_REVISION")));
        }

        const QJsonValue spotValue = params.value(QStringLiteral("spot"));
        if (!spotValue.isObject()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("spot must be an object"),
                               errorData(QStringLiteral("INVALID_SPOT")));
        }
        SpotSpec spot;
        QString parseError;
        if (!SpotSpec::fromJson(spotValue.toObject(), &spot, &parseError)) {
            QJsonObject data = errorData(QStringLiteral("INVALID_SPOT"));
            data.insert(QStringLiteral("detail"), parseError);
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("Invalid spot payload"), data);
        }

        const SetSpotResult result = m_service->setSpot(spot, expectedRevision);
        if (result.status == SetSpotStatus::Invalid) {
            QJsonObject data = errorData(QStringLiteral("SPOT_INVALID"));
            data.insert(QStringLiteral("errors"),
                        result.validation.toJson().value(QStringLiteral("errors")));
            data.insert(QStringLiteral("warnings"),
                        result.validation.toJson().value(QStringLiteral("warnings")));
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("Spot validation failed"), data);
        }
        if (result.status == SetSpotStatus::Conflict) {
            QJsonObject data = errorData(QStringLiteral("STATE_CONFLICT"));
            data.insert(QStringLiteral("expectedRevision"),
                        static_cast<double>(expectedRevision));
            data.insert(QStringLiteral("actualRevision"),
                        static_cast<double>(result.actualRevision));
            return makeFailure(id, ErrorCode::StateConflict,
                               QStringLiteral("State revision conflict"), data);
        }

        QJsonObject response;
        response.insert(QStringLiteral("revision"), static_cast<double>(result.revision));
        response.insert(QStringLiteral("normalizedSpot"), result.normalizedSpot.toJson());
        return makeSuccess(id, response);
    }
    if (method == QStringLiteral("solve.start")) {
        if (!capabilityEnabled(QStringLiteral("solve.control"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("solve.control")}}));
        }
        qint64 expectedRevision = 0;
        QString revisionError;
        if (!parseRevision(params.value(QStringLiteral("expectedRevision")),
                           &expectedRevision, &revisionError)) {
            return makeFailure(id, ErrorCode::InvalidParams, revisionError,
                               errorData(QStringLiteral("INVALID_REVISION")));
        }

        const QJsonValue optionsValue = params.value(QStringLiteral("options"));
        if (!optionsValue.isObject()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("options must be an object"),
                               errorData(QStringLiteral("INVALID_SOLVE_OPTIONS")));
        }
        SolveOptions options;
        QString optionsError;
        if (!SolveOptions::fromJson(optionsValue.toObject(), &options, &optionsError)) {
            QJsonObject data = errorData(QStringLiteral("INVALID_SOLVE_OPTIONS"));
            data.insert(QStringLiteral("detail"), optionsError);
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("Invalid solve options"), data);
        }

        const StartSolveResult result = m_service->startSolve(options, expectedRevision);
        if (!result.success) {
            if (result.errorCode == QStringLiteral("STATE_CONFLICT")) {
                QJsonObject data = errorData(QStringLiteral("STATE_CONFLICT"));
                data.insert(QStringLiteral("expectedRevision"),
                            static_cast<double>(expectedRevision));
                data.insert(QStringLiteral("actualRevision"),
                            static_cast<double>(result.actualRevision));
                return makeFailure(id, ErrorCode::StateConflict,
                                   QStringLiteral("State revision conflict"), data);
            }
            if (result.errorCode == QStringLiteral("SPOT_INVALID")) {
                QJsonObject data = errorData(QStringLiteral("SPOT_INVALID"));
                data.insert(QStringLiteral("errors"),
                            result.validation.toJson().value(QStringLiteral("errors")));
                return makeFailure(id, ErrorCode::InvalidParams,
                                   QStringLiteral("Current spot is not valid"), data);
            }
            if (result.errorCode == QStringLiteral("SOLVER_BUSY")) {
                return makeFailure(id, ErrorCode::SolverBusy,
                                   QStringLiteral("Another solve job is already active"),
                                   errorData(QStringLiteral("SOLVER_BUSY")));
            }
            if (result.errorCode == QStringLiteral("SOLVER_UNAVAILABLE")) {
                return makeFailure(id, ErrorCode::SolverUnavailable,
                                   QStringLiteral("Solver is unavailable"),
                                   errorData(QStringLiteral("SOLVER_UNAVAILABLE")));
            }
            QJsonObject data = errorData(result.errorCode.isEmpty()
                                             ? QStringLiteral("SOLVER_START_FAILED")
                                             : result.errorCode);
            data.insert(QStringLiteral("detail"), result.errorMessage);
            return makeFailure(id, ErrorCode::SolverStartFailed,
                               QStringLiteral("Solver failed to start"), data);
        }

        QJsonObject response;
        response.insert(QStringLiteral("jobId"), result.status.jobId);
        response.insert(QStringLiteral("state"), QString::fromLatin1(JobState::Queued));
        return makeSuccess(id, response);
    }
    if (method == QStringLiteral("solve.getStatus")) {
        if (!capabilityEnabled(QStringLiteral("solve.control"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("solve.control")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        bool found = false;
        const JobStatus status = m_service->getJobStatus(jobIdValue.toString(), &found);
        if (!found) {
            return makeFailure(id, ErrorCode::JobNotFound,
                               QStringLiteral("Solve job was not found"),
                               errorData(QStringLiteral("JOB_NOT_FOUND"),
                                         QJsonObject{{QStringLiteral("jobId"),
                                                      jobIdValue.toString()}}));
        }
        return makeSuccess(id, status.toJson());
    }

    if (method == QStringLiteral("solve.cancel")) {
        if (!capabilityEnabled(QStringLiteral("solve.control"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("solve.control")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        const CancelSolveResult result = m_service->cancelSolve(jobIdValue.toString());
        if (!result.success) {
            const bool notFound = result.errorCode == QStringLiteral("JOB_NOT_FOUND");
            return makeFailure(id,
                               notFound ? ErrorCode::JobNotFound : ErrorCode::JobNotActive,
                               result.errorMessage,
                               errorData(result.errorCode));
        }
        return makeSuccess(id, result.status.toJson());
    }
    if (method == QStringLiteral("solve.getSummary")) {
        if (!capabilityEnabled(QStringLiteral("result.read"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("result.read")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        QJsonObject result;
        QString errorCode;
        QString errorMessage;
        if (!m_service->getSolveSummary(jobIdValue.toString(), &result,
                                        &errorCode, &errorMessage)) {
            return makeFailure(id, resultErrorRpcCode(errorCode), errorMessage,
                               errorData(errorCode));
        }
        return makeSuccess(id, result);
    }

    if (method == QStringLiteral("result.getExploitability")) {
        if (!capabilityEnabled(QStringLiteral("result.read"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("result.read")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        QJsonObject result;
        QString errorCode;
        QString errorMessage;
        if (!m_service->getExploitability(jobIdValue.toString(), &result,
                                          &errorCode, &errorMessage)) {
            return makeFailure(id, resultErrorRpcCode(errorCode), errorMessage,
                               errorData(errorCode));
        }
        return makeSuccess(id, result);
    }

    if (method == QStringLiteral("artifact.getMetadata")) {
        if (!capabilityEnabled(QStringLiteral("artifact.read"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("artifact.read")}}));
        }
        const QJsonValue artifactIdValue = params.value(QStringLiteral("artifactId"));
        if (!artifactIdValue.isString() || artifactIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("artifactId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_ARTIFACT_ID")));
        }
        QJsonObject result;
        QString errorCode;
        QString errorMessage;
        if (!m_service->getArtifactMetadata(artifactIdValue.toString(), &result,
                                            &errorCode, &errorMessage)) {
            return makeFailure(id, resultErrorRpcCode(errorCode), errorMessage,
                               errorData(errorCode));
        }
        return makeSuccess(id, result);
    }
    if (method == QStringLiteral("result.getNodeStrategy")
        || method == QStringLiteral("result.getActionEv")) {
        if (!capabilityEnabled(QStringLiteral("result.read"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("result.read")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        const QJsonValue nodeIdValue = params.value(QStringLiteral("nodeId"));
        if (!nodeIdValue.isString() || nodeIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("nodeId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_NODE_ID")));
        }

        QStringList actionFilter;
        QString filterError;
        if (!readStringArray(params, QStringLiteral("actions"),
                             &actionFilter, &filterError)) {
            return makeFailure(id, ErrorCode::InvalidParams, filterError,
                               errorData(QStringLiteral("INVALID_ACTIONS")));
        }

        int limit = 100;
        if (params.contains(QStringLiteral("limit"))) {
            const QJsonValue limitValue = params.value(QStringLiteral("limit"));
            if (!limitValue.isDouble() || limitValue.toDouble() < 1
                || limitValue.toDouble() > 100
                || std::floor(limitValue.toDouble()) != limitValue.toDouble()) {
                return makeFailure(id, ErrorCode::InvalidParams,
                                   QStringLiteral("limit must be an integer between 1 and 100"),
                                   errorData(QStringLiteral("INVALID_LIMIT")));
            }
            limit = static_cast<int>(limitValue.toDouble());
        }

        QJsonObject result;
        QString errorCode;
        QString errorMessage;
        const bool isNodeStrategy =
            method == QStringLiteral("result.getNodeStrategy");
        const bool queried = isNodeStrategy
                                 ? m_service->getNodeStrategy(jobIdValue.toString(),
                                                              nodeIdValue.toString(),
                                                              actionFilter, limit,
                                                              &result, &errorCode,
                                                              &errorMessage)
                                 : m_service->getActionEv(jobIdValue.toString(),
                                                          nodeIdValue.toString(),
                                                          actionFilter, limit,
                                                          &result, &errorCode,
                                                          &errorMessage);
        if (!queried) {
            return makeFailure(id, resultErrorRpcCode(errorCode), errorMessage,
                               errorData(errorCode));
        }
        return makeSuccess(id, result);
    }
    if (method == QStringLiteral("ui.focusNode")) {
        if (!capabilityEnabled(QStringLiteral("ui.focus"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("ui.focus")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        const QJsonValue nodeIdValue = params.value(QStringLiteral("nodeId"));
        if (!nodeIdValue.isString() || nodeIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("nodeId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_NODE_ID")));
        }
        const UiFocusResult result =
            m_service->focusNode(jobIdValue.toString(), nodeIdValue.toString());
        if (!result.success) {
            return makeFailure(id, resultErrorRpcCode(result.errorCode),
                               result.errorMessage, errorData(result.errorCode));
        }
        QJsonObject response;
        response.insert(QStringLiteral("jobId"), result.jobId);
        response.insert(QStringLiteral("nodeId"), result.nodeId);
        response.insert(QStringLiteral("revision"), static_cast<double>(result.revision));
        return makeSuccess(id, response);
    }

    if (method == QStringLiteral("ui.openResult")) {
        if (!capabilityEnabled(QStringLiteral("ui.focus"))) {
            return makeFailure(id, ErrorCode::CapabilityDisabled,
                               QStringLiteral("Capability is disabled"),
                               errorData(QStringLiteral("CAPABILITY_DISABLED"),
                                         QJsonObject{{QStringLiteral("capability"),
                                                      QStringLiteral("ui.focus")}}));
        }
        const QJsonValue jobIdValue = params.value(QStringLiteral("jobId"));
        if (!jobIdValue.isString() || jobIdValue.toString().isEmpty()) {
            return makeFailure(id, ErrorCode::InvalidParams,
                               QStringLiteral("jobId must be a non-empty string"),
                               errorData(QStringLiteral("INVALID_JOB_ID")));
        }
        const UiOpenResult result = m_service->openResult(jobIdValue.toString());
        if (!result.success) {
            return makeFailure(id, resultErrorRpcCode(result.errorCode),
                               result.errorMessage, errorData(result.errorCode));
        }
        QJsonObject response;
        response.insert(QStringLiteral("jobId"), result.jobId);
        response.insert(QStringLiteral("opened"), true);
        return makeSuccess(id, response);
    }

    return makeFailure(id, ErrorCode::MethodNotFound,
                       QStringLiteral("Method not found"),
                       errorData(QStringLiteral("METHOD_NOT_FOUND"),
                                 QJsonObject{{QStringLiteral("method"), method}}));
}

QJsonObject LocalApiServer::handleHello(QWebSocket *socket, const QJsonObject &params)
{
    ClientState &client = m_clients[socket];
    client.authenticated = true;
    const QJsonObject clientInfo = params.value(QStringLiteral("client")).toObject();
    client.clientName = clientInfo.value(QStringLiteral("name")).toString();
    client.clientVersion = clientInfo.value(QStringLiteral("version")).toString();

    QJsonObject server;
    server.insert(QStringLiteral("name"), QString::fromLatin1(ServerName));
    server.insert(QStringLiteral("version"), QString::fromLatin1(ServerVersion));
    server.insert(QStringLiteral("commit"), QString::fromLatin1(ServerCommit));

    QJsonObject result;
    result.insert(QStringLiteral("protocol"), QString::fromLatin1(ProtocolVersion));
    result.insert(QStringLiteral("sessionId"), m_service->sessionId());
    result.insert(QStringLiteral("server"), server);
    result.insert(QStringLiteral("capabilities"), capabilitiesArray());
    return result;
}
QJsonObject LocalApiServer::capabilitiesResult() const
{
    QJsonObject result = m_service->getCapabilities();
    result.insert(QStringLiteral("capabilities"), capabilitiesArray());

    QJsonArray filteredMethods;
    const QJsonArray methods = result.value(QStringLiteral("methods")).toArray();
    for (const QJsonValue &value : methods) {
        const QJsonObject method = value.toObject();
        const QJsonValue capability = method.value(QStringLiteral("capability"));
        if (capability.isNull()
            || (capability.isString() && capabilityEnabled(capability.toString()))) {
            filteredMethods.append(method);
        }
    }
    result.insert(QStringLiteral("methods"), filteredMethods);
    return result;
}
QJsonArray LocalApiServer::capabilitiesArray() const
{
    QJsonArray capabilities;
    for (const QString &capability : m_service->capabilities()) {
        if (m_enabledCapabilities.contains(capability)) {
            capabilities.append(capability);
        }
    }
    return capabilities;
}

bool LocalApiServer::capabilityEnabled(const QString &capability) const
{
    return m_enabledCapabilities.contains(capability);
}

void LocalApiServer::sendJson(QWebSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    socket->sendTextMessage(QString::fromUtf8(
        QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void LocalApiServer::sendError(QWebSocket *socket,
                               const QJsonValue &id,
                               int code,
                               const QString &message,
                               const QJsonValue &data)
{
    sendJson(socket, makeFailure(id, code, message, data));
}

void LocalApiServer::broadcastEvent(const QString &method, const QJsonObject &params)
{
    if (m_clients.isEmpty()) {
        return;
    }

    QJsonObject event;
    event.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    event.insert(QStringLiteral("method"), method);
    event.insert(QStringLiteral("params"), params);
    const QString payload = QString::fromUtf8(
        QJsonDocument(event).toJson(QJsonDocument::Compact));

    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (it.value().authenticated
            && it.key()
            && it.key()->state() == QAbstractSocket::ConnectedState) {
            it.key()->sendTextMessage(payload);
        }
    }
}

bool LocalApiServer::writeDiscoveryFile(QString *errorMessage)
{
    const QFileInfo fileInfo(m_discoveryFilePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create API discovery directory: %1")
                                .arg(fileInfo.absolutePath());
        }
        return false;
    }

    QJsonObject discovery;
    discovery.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    discovery.insert(QStringLiteral("protocol"), QString::fromLatin1(ProtocolVersion));
    discovery.insert(QStringLiteral("port"), m_port);
    discovery.insert(QStringLiteral("token"), m_token);
    discovery.insert(QStringLiteral("serverVersion"), QString::fromLatin1(ServerVersion));
    discovery.insert(QStringLiteral("startedAt"), m_startedAt);

    QSaveFile file(m_discoveryFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open API discovery file: %1")
                                .arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(discovery).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write API discovery file: %1")
                                .arg(file.errorString());
        }
        return false;
    }

    QFile::setPermissions(m_discoveryFilePath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void LocalApiServer::removeDiscoveryFile()
{
    if (m_discoveryFilePath.isEmpty() || m_token.isEmpty()) {
        return;
    }

    QFile file(m_discoveryFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        return;
    }
    if (document.object().value(QStringLiteral("token")).toString() == m_token) {
        QFile::remove(m_discoveryFilePath);
    }
}

} // namespace GtoApi