#ifndef GTO_API_LOCAL_API_SERVER_H
#define GTO_API_LOCAL_API_SERVER_H

#include "protocol.h"

#include <QJsonArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

class QWebSocket;
class QWebSocketServer;

namespace GtoApi {

class GtoApplicationService;

struct ApiServerConfig
{
    quint16 port = 0;
    QString discoveryFilePath;
    QStringList enabledCapabilities = {QStringLiteral("state.read"), QStringLiteral("spot.write"), QStringLiteral("solve.control"), QStringLiteral("result.read"), QStringLiteral("artifact.read"), QStringLiteral("ui.focus")};
};

class LocalApiServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalApiServer(GtoApplicationService *service, QObject *parent = nullptr);
    ~LocalApiServer() override;

    bool start(const ApiServerConfig &config, QString *errorMessage = nullptr);
    void stop();

    bool isListening() const;
    quint16 port() const;
    QString token() const;
    QString discoveryFilePath() const;

    static QString defaultDiscoveryFilePath();

private slots:
    void onNewConnection();
    void onTextMessage(QWebSocket *socket, const QString &message);
    void onSocketDisconnected();

private:
    struct ClientState
    {
        bool authenticated = false;
        QString clientName;
        QString clientVersion;
    };

    QJsonObject dispatch(QWebSocket *socket, const QJsonObject &request);
    QJsonObject handleHello(QWebSocket *socket, const QJsonObject &params);
    QJsonObject capabilitiesResult() const;
    QJsonArray capabilitiesArray() const;
    bool capabilityEnabled(const QString &capability) const;

    void sendJson(QWebSocket *socket, const QJsonObject &message);
    void sendError(QWebSocket *socket,
                   const QJsonValue &id,
                   int code,
                   const QString &message,
                   const QJsonValue &data = QJsonValue::Undefined);
    void broadcastEvent(const QString &method, const QJsonObject &params);

    bool writeDiscoveryFile(QString *errorMessage);
    void removeDiscoveryFile();

    GtoApplicationService *m_service = nullptr;
    QWebSocketServer *m_server = nullptr;
    QSet<QString> m_enabledCapabilities;
    QHash<QWebSocket *, ClientState> m_clients;
    QString m_token;
    QString m_discoveryFilePath;
    QString m_startedAt;
    quint16 m_port = 0;
};

} // namespace GtoApi

#endif // GTO_API_LOCAL_API_SERVER_H