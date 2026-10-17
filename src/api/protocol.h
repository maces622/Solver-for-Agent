#ifndef GTO_API_PROTOCOL_H
#define GTO_API_PROTOCOL_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace GtoApi {

constexpr const char *ProtocolVersion = "gto-api/1.0";
constexpr const char *ServerName = "TexasSolverGui";
constexpr const char *ServerVersion = "0.1.0";
constexpr const char *ServerCommit = "unknown";
constexpr int MaxMessageBytes = 1024 * 1024;

namespace ErrorCode {
constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;
constexpr int Unauthorized = -32001;
constexpr int ProtocolMismatch = -32002;
constexpr int CapabilityDisabled = -32003;
constexpr int StateConflict = -32010;
constexpr int SolverBusy = -32011;
constexpr int SolverUnavailable = -32012;
constexpr int JobNotFound = -32013;
constexpr int JobNotActive = -32014;
constexpr int SolverStartFailed = -32015;
constexpr int ResultNotReady = -32016;
constexpr int ArtifactNotFound = -32017;
constexpr int NodeNotFound = -32018;
} // namespace ErrorCode

QJsonObject makeSuccess(const QJsonValue &id, const QJsonObject &result);
QJsonObject makeFailure(const QJsonValue &id,
                        int code,
                        const QString &message,
                        const QJsonValue &data = QJsonValue::Undefined);
QJsonObject errorData(const QString &code,
                      const QJsonObject &details = QJsonObject());

} // namespace GtoApi

#endif // GTO_API_PROTOCOL_H