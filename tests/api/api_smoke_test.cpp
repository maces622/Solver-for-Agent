#include "src/api/api_server.h"
#include "src/api/services/gto_application_service.h"
#include "src/api/services/solver_job_runner.h"
#include "src/api/services/strategy_result_provider.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

using namespace GtoApi;

class FakeSolverRunner : public SolverJobRunner
{
    Q_OBJECT

public:
    explicit FakeSolverRunner(QObject *parent = nullptr)
        : SolverJobRunner(parent) {}

    bool start(const SpotSpec &, const SolveOptions &, QString * = nullptr) override
    {
        ++startCount;
        if (startCount == 1) {
            QTimer::singleShot(10, this, [this]() {
                emit progress(20, 100, 1.5);
            });
            QTimer::singleShot(20, this, [this]() {
                emit progress(100, 100, 0.2);
            });
            QTimer::singleShot(30, this, [this]() {
                emit completed();
            });
        }
        return true;
    }

    void cancel() override
    {
        QTimer::singleShot(5, this, [this]() {
            emit progress(50, 100, 1.0);
        });
        QTimer::singleShot(10, this, [this]() {
            emit cancelled();
        });
    }

    int startCount = 0;
};

class FakeStrategyResultProvider : public StrategyResultProvider
{
    Q_OBJECT

public:
    explicit FakeStrategyResultProvider(QObject *parent = nullptr)
        : StrategyResultProvider(parent) {}

    bool getTreeSummary(const QString &, ResultTreeSummary *summary,
                        QString * = nullptr, QString * = nullptr) override
    {
        summary->rootNodeId = QStringLiteral("root");
        summary->nodeCount = 12345;
        return true;
    }

    bool queryNode(const QString &, const QString &nodeId,
                   const QStringList &, int,
                   NodeQueryResult *result,
                   QString * = nullptr, QString * = nullptr) override
    {
        QJsonObject check;
        check.insert(QStringLiteral("action"), QStringLiteral("check"));
        check.insert(QStringLiteral("frequency"), 0.62);
        check.insert(QStringLiteral("ev"), 2.31);
        QJsonObject bet;
        bet.insert(QStringLiteral("action"), QStringLiteral("bet_33"));
        bet.insert(QStringLiteral("frequency"), 0.38);
        bet.insert(QStringLiteral("ev"), 2.28);

        QJsonObject artifactContent;
        artifactContent.insert(QStringLiteral("nodeId"), nodeId);
        artifactContent.insert(QStringLiteral("matrix"), QJsonArray{0.62, 0.38});

        result->nodeId = nodeId;
        result->actions = QJsonArray{check, bet};
        result->comboCount = 10;
        result->artifactContent = artifactContent;
        return true;
    }
};

class ApiSmokeTest : public QObject
{
    Q_OBJECT

public:
    explicit ApiSmokeTest(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_service = new GtoApplicationService(this);
        m_runner = new FakeSolverRunner(this);
        m_service->setSolverJobRunner(m_runner);
        m_resultProvider = new FakeStrategyResultProvider(this);
        m_service->setStrategyResultProvider(m_resultProvider);
        m_server = new LocalApiServer(m_service, this);

        connect(&m_socket, &QWebSocket::connected, this, &ApiSmokeTest::onConnected);
        connect(&m_socket, &QWebSocket::textMessageReceived,
                this, &ApiSmokeTest::onTextMessage);
        connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                this, [this](QAbstractSocket::SocketError) {
            qCritical().noquote() << QStringLiteral("WebSocket error: %1")
                                         .arg(m_socket.errorString());
        });

        QTimer::singleShot(0, this, &ApiSmokeTest::start);
        QTimer::singleShot(15000, this, [this]() {
            fail(QStringLiteral("Smoke test timed out"));
        });
    }

private slots:
    void start()
    {
        m_discoveryPath = QDir::temp().filePath(
            QStringLiteral("gto-api-smoke-%1.json")
                .arg(QCoreApplication::applicationPid()));

        ApiServerConfig config;
        config.port = 0;
        config.discoveryFilePath = m_discoveryPath;
        QString errorMessage;
        if (!m_server->start(config, &errorMessage)) {
            fail(QStringLiteral("Server start failed: %1").arg(errorMessage));
            return;
        }
        if (m_server->port() == 0) {
            fail(QStringLiteral("Server did not allocate a port"));
            return;
        }
        if (m_server->token().size() < 32) {
            fail(QStringLiteral("Server token is too short"));
            return;
        }

        QFile discovery(m_discoveryPath);
        if (!discovery.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Discovery file was not created"));
            return;
        }
        const QJsonObject discoveryObject =
            QJsonDocument::fromJson(discovery.readAll()).object();
        if (discoveryObject.value(QStringLiteral("token")).toString()
            != m_server->token()) {
            fail(QStringLiteral("Discovery token does not match"));
            return;
        }

        m_socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/rpc")
                               .arg(m_server->port())));
    }

    void onConnected()
    {
        QJsonObject client;
        client.insert(QStringLiteral("name"), QStringLiteral("api-smoke-test"));
        client.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));

        QJsonObject helloParams;
        helloParams.insert(QStringLiteral("protocol"), QString::fromLatin1(ProtocolVersion));
        helloParams.insert(QStringLiteral("token"), m_server->token());
        helloParams.insert(QStringLiteral("client"), client);
        sendRequest(1, QStringLiteral("system.hello"), helloParams);
    }

    void onTextMessage(const QString &message)
    {
        const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
        if (!document.isObject()) {
            fail(QStringLiteral("Received a non-object WebSocket message"));
            return;
        }
        const QJsonObject object = document.object();

        if (object.contains(QStringLiteral("method"))) {
            handleEvent(object);
            return;
        }

        if (object.contains(QStringLiteral("error"))) {
            const int errorId = object.value(QStringLiteral("id")).toInt();
            const QJsonObject error = object.value(QStringLiteral("error")).toObject();
            const QJsonObject data = error.value(QStringLiteral("data")).toObject();
            if (errorId == 8
                && error.value(QStringLiteral("code")).toInt() == ErrorCode::StateConflict
                && data.value(QStringLiteral("code")).toString()
                       == QStringLiteral("STATE_CONFLICT")
                && data.value(QStringLiteral("expectedRevision")).toInt() == 0
                && data.value(QStringLiteral("actualRevision")).toInt() == 1) {
                m_conflictSeen = true;
                QJsonObject options;
                options.insert(QStringLiteral("maxIterations"), 100);
                options.insert(QStringLiteral("targetExploitability"), 0.2);
                options.insert(QStringLiteral("threads"), 1);
                options.insert(QStringLiteral("algorithm"), QStringLiteral("discounted_cfr"));
                options.insert(QStringLiteral("printInterval"), 1);
                options.insert(QStringLiteral("dumpRounds"), 2);
                QJsonObject params;
                params.insert(QStringLiteral("expectedRevision"), 1);
                params.insert(QStringLiteral("options"), options);
                sendRequest(9, QStringLiteral("solve.start"), params);
                return;
            }
            fail(QStringLiteral("JSON-RPC error: %1")
                     .arg(QString::fromUtf8(QJsonDocument(object)
                                                .toJson(QJsonDocument::Compact))));
            return;
        }

        const int id = object.value(QStringLiteral("id")).toInt();
        const QJsonObject result = object.value(QStringLiteral("result")).toObject();
        if (id == 1) {
            const QJsonArray capabilities =
                result.value(QStringLiteral("capabilities")).toArray();
            if (!capabilities.contains(QStringLiteral("state.read"))
                || !capabilities.contains(QStringLiteral("spot.write"))
                || !capabilities.contains(QStringLiteral("solve.control"))
                || !capabilities.contains(QStringLiteral("result.read"))
                || !capabilities.contains(QStringLiteral("artifact.read"))) {
                fail(QStringLiteral("Expected capabilities are missing"));
                return;
            }
            if (result.value(QStringLiteral("sessionId")).toString().isEmpty()) {
                fail(QStringLiteral("sessionId is missing from hello response"));
                return;
            }
            sendRequest(2, QStringLiteral("system.getCapabilities"), QJsonObject());
            return;
        }
        if (id == 2) {
            const QJsonArray methods = result.value(QStringLiteral("methods")).toArray();
            bool hasStateGet = false;
            for (const QJsonValue &value : methods) {
                if (value.toObject().value(QStringLiteral("name")).toString()
                    == QStringLiteral("state.get")) {
                    hasStateGet = true;
                }
            }
            if (!hasStateGet) {
                fail(QStringLiteral("state.get is missing from capabilities"));
                return;
            }
            sendRequest(3, QStringLiteral("state.get"), QJsonObject());
            return;
        }
        if (id == 3) {
            if (result.value(QStringLiteral("revision")).toInt() != 0) {
                fail(QStringLiteral("Initial revision is not 0"));
                return;
            }
            if (!result.value(QStringLiteral("spot")).isObject()) {
                fail(QStringLiteral("Initial state does not include a spot"));
                return;
            }

            QJsonObject params;
            params.insert(QStringLiteral("spot"), spotPayload());
            sendRequest(4, QStringLiteral("spot.validate"), params);
            return;
        }
        if (id == 4) {
            if (!result.value(QStringLiteral("valid")).toBool()) {
                fail(QStringLiteral("Valid spot was rejected: %1")
                         .arg(QString::fromUtf8(QJsonDocument(result)
                                                    .toJson(QJsonDocument::Compact))));
                return;
            }
            if (!result.value(QStringLiteral("normalizedSpot")).isObject()) {
                fail(QStringLiteral("spot.validate did not return normalizedSpot"));
                return;
            }

            QJsonArray board;
            board.append(QStringLiteral("Ah"));
            board.append(QStringLiteral("Kd"));
            board.append(QStringLiteral("7c"));
            QJsonObject params;
            params.insert(QStringLiteral("range"), QStringLiteral("AA,KK"));
            params.insert(QStringLiteral("board"), board);
            params.insert(QStringLiteral("mode"), QStringLiteral("holdem"));
            sendRequest(5, QStringLiteral("range.validate"), params);
            return;
        }
        if (id == 5) {
            if (!result.value(QStringLiteral("valid")).toBool()
                || result.value(QStringLiteral("comboCount")).toInt() != 6) {
                fail(QStringLiteral("range.validate returned an unexpected result: %1")
                         .arg(QString::fromUtf8(QJsonDocument(result)
                                                    .toJson(QJsonDocument::Compact))));
                return;
            }

            QJsonObject params;
            params.insert(QStringLiteral("expectedRevision"), 0);
            params.insert(QStringLiteral("spot"), spotPayload());
            sendRequest(6, QStringLiteral("spot.set"), params);
            return;
        }
        if (id == 6) {
            if (result.value(QStringLiteral("revision")).toInt() != 1) {
                fail(QStringLiteral("spot.set did not advance revision to 1"));
                return;
            }
            sendRequest(7, QStringLiteral("state.get"), QJsonObject());
            return;
        }
        if (id == 7) {
            if (result.value(QStringLiteral("revision")).toInt() != 1) {
                fail(QStringLiteral("Revision did not advance to 1"));
                return;
            }
            if (result.value(QStringLiteral("spot")).toObject()
                    .value(QStringLiteral("pot")).toDouble() != 77.0) {
                fail(QStringLiteral("Updated spot was not returned"));
                return;
            }
            m_stateResponseSeen = true;

            QJsonObject params;
            params.insert(QStringLiteral("expectedRevision"), 0);
            params.insert(QStringLiteral("spot"), spotPayload());
            sendRequest(8, QStringLiteral("spot.set"), params);
            return;
        }
        if (id == 9) {
            m_job1 = result.value(QStringLiteral("jobId")).toString();
            if (m_job1.isEmpty()
                || result.value(QStringLiteral("state")).toString()
                       != QStringLiteral("queued")) {
                fail(QStringLiteral("solve.start returned an unexpected result"));
                return;
            }
            QJsonObject params;
            params.insert(QStringLiteral("jobId"), m_job1);
            sendRequest(10, QStringLiteral("solve.getStatus"), params);
            requestCompletedStateIfReady();
            return;
        }
        if (id == 10) {
            if (result.value(QStringLiteral("jobId")).toString() != m_job1) {
                fail(QStringLiteral("solve.getStatus returned the wrong jobId"));
                return;
            }
            const QString state = result.value(QStringLiteral("state")).toString();
            if (state != QStringLiteral("queued")
                && state != QStringLiteral("running")
                && state != QStringLiteral("completed")) {
                fail(QStringLiteral("Unexpected solve state: %1").arg(state));
                return;
            }
            return;
        }
        if (id == 11) {
            const QJsonObject solve = result.value(QStringLiteral("solve")).toObject();
            if (solve.value(QStringLiteral("jobId")).toString() != m_job1
                || solve.value(QStringLiteral("state")).toString()
                       != QStringLiteral("completed")
                || solve.value(QStringLiteral("progress")).toDouble() != 1.0) {
                fail(QStringLiteral("Completed solve state is not reflected in state.get"));
                return;
            }
            m_completedStatusSeen = true;
            m_lastRevision = result.value(QStringLiteral("revision")).toInt();

            QJsonObject summaryParams;
            summaryParams.insert(QStringLiteral("jobId"), m_job1);
            sendRequest(20, QStringLiteral("solve.getSummary"), summaryParams);
            return;
        }
        if (id == 20) {
            if (result.value(QStringLiteral("state")).toString()
                    != QStringLiteral("completed")
                || result.value(QStringLiteral("tree")).toObject()
                       .value(QStringLiteral("nodeCount")).toInt() != 12345) {
                fail(QStringLiteral("solve.getSummary returned an unexpected result"));
                return;
            }
            m_summarySeen = true;

            QJsonObject params;
            params.insert(QStringLiteral("jobId"), m_job1);
            params.insert(QStringLiteral("nodeId"), QStringLiteral("root/a0"));
            params.insert(QStringLiteral("limit"), 100);
            sendRequest(21, QStringLiteral("result.getNodeStrategy"), params);
            return;
        }
        if (id == 21) {
            m_artifact1 = result.value(QStringLiteral("artifactId")).toString();
            if (result.value(QStringLiteral("actions")).toArray().size() != 2
                || m_artifact1.isEmpty()
                || result.value(QStringLiteral("artifactRef")).toString().isEmpty()) {
                fail(QStringLiteral("result.getNodeStrategy returned an unexpected result"));
                return;
            }
            m_nodeStrategySeen = true;

            QJsonObject params;
            params.insert(QStringLiteral("jobId"), m_job1);
            params.insert(QStringLiteral("nodeId"), QStringLiteral("root/a0"));
            sendRequest(22, QStringLiteral("result.getActionEv"), params);
            return;
        }
        if (id == 22) {
            if (result.value(QStringLiteral("actions")).toArray().isEmpty()) {
                fail(QStringLiteral("result.getActionEv returned no actions"));
                return;
            }
            m_actionEvSeen = true;

            QJsonObject params;
            params.insert(QStringLiteral("jobId"), m_job1);
            sendRequest(23, QStringLiteral("result.getExploitability"), params);
            return;
        }
        if (id == 23) {
            if (result.value(QStringLiteral("exploitability")).toDouble() != 0.2) {
                fail(QStringLiteral("result.getExploitability returned an unexpected value"));
                return;
            }
            m_exploitabilitySeen = true;

            QJsonObject params;
            params.insert(QStringLiteral("artifactId"), m_artifact1);
            sendRequest(24, QStringLiteral("artifact.getMetadata"), params);
            return;
        }
        if (id == 24) {
            if (result.value(QStringLiteral("artifactId")).toString() != m_artifact1
                || result.value(QStringLiteral("sha256")).toString().isEmpty()) {
                fail(QStringLiteral("artifact.getMetadata returned an unexpected result"));
                return;
            }
            m_artifactSeen = true;

            QJsonObject options;
            options.insert(QStringLiteral("maxIterations"), 100);
            options.insert(QStringLiteral("targetExploitability"), 0.2);
            options.insert(QStringLiteral("threads"), 1);
            options.insert(QStringLiteral("algorithm"), QStringLiteral("discounted_cfr"));
            options.insert(QStringLiteral("printInterval"), 1);
            options.insert(QStringLiteral("dumpRounds"), 2);
            QJsonObject params;
            params.insert(QStringLiteral("expectedRevision"), m_lastRevision);
            params.insert(QStringLiteral("options"), options);
            sendRequest(12, QStringLiteral("solve.start"), params);
            return;
        }
        if (id == 12) {
            m_job2 = result.value(QStringLiteral("jobId")).toString();
            if (m_job2.isEmpty()) {
                fail(QStringLiteral("Second solve.start did not return a jobId"));
                return;
            }
            QJsonObject params;
            params.insert(QStringLiteral("jobId"), m_job2);
            sendRequest(13, QStringLiteral("solve.cancel"), params);
            return;
        }
        if (id == 13) {
            if (result.value(QStringLiteral("jobId")).toString() != m_job2) {
                fail(QStringLiteral("solve.cancel returned the wrong jobId"));
                return;
            }
            requestCancelledStateIfReady();
            return;
        }
        if (id == 14) {
            if (result.value(QStringLiteral("jobId")).toString() != m_job2
                || result.value(QStringLiteral("state")).toString()
                       != QStringLiteral("cancelled")) {
                fail(QStringLiteral("Cancelled solve state was not returned"));
                return;
            }
            m_cancelledStatusSeen = true;
            tryFinish();
            return;
        }

    }

private:
    QJsonObject spotPayload() const
    {
        QJsonObject sizing;
        sizing.insert(QStringLiteral("bet"), QJsonArray{33.0, 75.0});
        sizing.insert(QStringLiteral("raise"), QJsonArray{60.0});
        sizing.insert(QStringLiteral("allin"), false);
        QJsonObject street;
        street.insert(QStringLiteral("oop"), sizing);
        street.insert(QStringLiteral("ip"), sizing);
        QJsonObject betSizing;
        betSizing.insert(QStringLiteral("flop"), street);
        betSizing.insert(QStringLiteral("turn"), street);
        betSizing.insert(QStringLiteral("river"), street);

        QJsonObject ranges;
        ranges.insert(QStringLiteral("OOP"), QStringLiteral("AA,KK"));
        ranges.insert(QStringLiteral("IP"), QStringLiteral("QQ,JJ"));

        QJsonObject spot;
        spot.insert(QStringLiteral("gameType"), QStringLiteral("NLHE"));
        spot.insert(QStringLiteral("mode"), QStringLiteral("holdem"));
        spot.insert(QStringLiteral("positions"), QJsonArray{QStringLiteral("OOP"),
                                                           QStringLiteral("IP")});
        spot.insert(QStringLiteral("effectiveStackBb"), 100.0);
        spot.insert(QStringLiteral("blinds"), QJsonArray{0.5, 1.0});
        spot.insert(QStringLiteral("pot"), 77.0);
        spot.insert(QStringLiteral("board"), QJsonArray{QStringLiteral("Ah"),
                                                        QStringLiteral("Kd"),
                                                        QStringLiteral("7c")});
        spot.insert(QStringLiteral("ranges"), ranges);
        spot.insert(QStringLiteral("betSizing"), betSizing);
        spot.insert(QStringLiteral("allinThreshold"), 0.67);
        spot.insert(QStringLiteral("useIsomorphism"), true);
        spot.insert(QStringLiteral("useHalfFloats"), false);
        return spot;
    }

    void sendRequest(int id, const QString &method, const QJsonObject &params)
    {
        QJsonObject request;
        request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        request.insert(QStringLiteral("id"), id);
        request.insert(QStringLiteral("method"), method);
        request.insert(QStringLiteral("params"), params);
        m_socket.sendTextMessage(QString::fromUtf8(
            QJsonDocument(request).toJson(QJsonDocument::Compact)));
    }

    void handleEvent(const QJsonObject &event)
    {
        const QString method = event.value(QStringLiteral("method")).toString();
        const QJsonObject params = event.value(QStringLiteral("params")).toObject();
        const int sequence = params.value(QStringLiteral("sequence")).toInt();
        if (sequence <= m_lastSequence) {
            fail(QStringLiteral("Event sequence is not monotonic"));
            return;
        }
        m_lastSequence = sequence;

        if (method == QStringLiteral("event.state_changed")) {
            return;
        }
        if (method == QStringLiteral("event.solve_queued")) {
            ++m_queuedEventCount;
            return;
        }
        if (method == QStringLiteral("event.solve_progress")) {
            ++m_progressEventCount;
            return;
        }
        if (method == QStringLiteral("event.solve_completed")) {
            if (params.value(QStringLiteral("jobId")).toString() != m_job1) {
                fail(QStringLiteral("solve_completed has the wrong jobId"));
                return;
            }
            m_completedEventSeen = true;
            requestCompletedStateIfReady();
            return;
        }
        if (method == QStringLiteral("event.solve_cancelled")) {
            if (params.value(QStringLiteral("jobId")).toString() != m_job2) {
                fail(QStringLiteral("solve_cancelled has the wrong jobId"));
                return;
            }
            m_cancelledEventSeen = true;
            requestCancelledStateIfReady();
            return;
        }
        if (method == QStringLiteral("event.solve_failed")) {
            fail(QStringLiteral("Unexpected solve_failed event"));
            return;
        }
        fail(QStringLiteral("Unexpected event method: %1").arg(method));
    }

    void requestCompletedStateIfReady()
    {
        if (m_job1.isEmpty() || !m_completedEventSeen || m_completedStateRequested) {
            return;
        }
        m_completedStateRequested = true;
        sendRequest(11, QStringLiteral("state.get"), QJsonObject());
    }

    void requestCancelledStateIfReady()
    {
        if (m_job2.isEmpty() || !m_cancelledEventSeen || m_cancelledStateRequested) {
            return;
        }
        m_cancelledStateRequested = true;
        QJsonObject params;
        params.insert(QStringLiteral("jobId"), m_job2);
        sendRequest(14, QStringLiteral("solve.getStatus"), params);
    }

    void tryFinish()
    {
        if (!m_stateResponseSeen || !m_conflictSeen
            || m_queuedEventCount < 2 || m_progressEventCount == 0
            || !m_completedEventSeen || !m_cancelledEventSeen
            || !m_completedStatusSeen || !m_cancelledStatusSeen
            || !m_summarySeen || !m_nodeStrategySeen || !m_actionEvSeen
            || !m_exploitabilitySeen || !m_artifactSeen) {
            return;
        }
        m_socket.close();
        m_server->stop();
        QFile::remove(m_discoveryPath);
        QCoreApplication::exit(0);
    }

    void fail(const QString &message)
    {
        qCritical().noquote() << message;
        m_socket.close();
        m_server->stop();
        QFile::remove(m_discoveryPath);
        QCoreApplication::exit(1);
    }

    GtoApplicationService *m_service = nullptr;
    FakeSolverRunner *m_runner = nullptr;
    FakeStrategyResultProvider *m_resultProvider = nullptr;
    LocalApiServer *m_server = nullptr;
    QWebSocket m_socket;
    QString m_discoveryPath;
    QString m_job1;
    QString m_job2;
    int m_lastSequence = 0;
    int m_queuedEventCount = 0;
    int m_progressEventCount = 0;
    bool m_stateResponseSeen = false;
    bool m_conflictSeen = false;
    bool m_completedEventSeen = false;
    bool m_cancelledEventSeen = false;
    bool m_completedStateRequested = false;
    bool m_cancelledStateRequested = false;
    bool m_completedStatusSeen = false;
    bool m_cancelledStatusSeen = false;
    bool m_summarySeen = false;
    bool m_nodeStrategySeen = false;
    bool m_actionEvSeen = false;
    bool m_exploitabilitySeen = false;
    bool m_artifactSeen = false;
    QString m_artifact1;
    int m_lastRevision = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString artifactRoot = QDir::temp().filePath(
        QStringLiteral("gto-api-artifacts-%1").arg(QCoreApplication::applicationPid()));
    qputenv("GTO_API_ARTIFACT_ROOT", artifactRoot.toUtf8());
    ApiSmokeTest test;
    const int exitCode = application.exec();
    QDir(artifactRoot).removeRecursively();
    return exitCode;
}

#include "api_smoke_test.moc"