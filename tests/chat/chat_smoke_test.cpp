#include "src/chat/acp_client.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTimer>

using namespace RangePilot;

class ChatSmokeTest : public QObject
{
    Q_OBJECT

public:
    explicit ChatSmokeTest(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_client, &AcpClient::sessionReady,
                this, &ChatSmokeTest::onSessionReady);
        connect(&m_client, &AcpClient::assistantText,
                this, &ChatSmokeTest::onAssistantText);
        connect(&m_client, &AcpClient::toolStatusChanged,
                this, &ChatSmokeTest::onToolStatus);
        connect(&m_client, &AcpClient::permissionRequested,
                this, &ChatSmokeTest::onPermission);
        connect(&m_client, &AcpClient::promptFinished,
                this, &ChatSmokeTest::onPromptFinished);
        connect(&m_client, &AcpClient::errorOccurred,
                this, &ChatSmokeTest::fail);
        QTimer::singleShot(0, this, &ChatSmokeTest::start);
        QTimer::singleShot(10000, this, [this]() {
            fail(QStringLiteral("ACP smoke test timed out"));
        });
    }

private slots:
    void start()
    {
        AcpClientConfig config;
        config.executable = qEnvironmentVariable("GTO_TEST_PYTHON");
        config.arguments = QStringList{
            qEnvironmentVariable("GTO_ACP_FAKE_SERVER"),
        };
        config.workingDirectory = QCoreApplication::applicationDirPath();
        if (config.executable.isEmpty() || config.arguments.first().isEmpty()) {
            fail(QStringLiteral("test environment is incomplete"));
            return;
        }
        m_client.start(config);
    }

    void onSessionReady(const QString &sessionId)
    {
        if (sessionId != QStringLiteral("session-test")) {
            fail(QStringLiteral("unexpected session id"));
            return;
        }
        m_client.prompt(QStringLiteral("test prompt"));
    }

    void onAssistantText(const QString &text)
    {
        if (text.contains(QStringLiteral("Hello from fake ACP"))) {
            m_sawText = true;
        }
    }

    void onToolStatus(const QString &, const QString &title, const QString &status)
    {
        if (title == QStringLiteral("Start GTO solve")
            && status == QStringLiteral("completed")) {
            m_sawTool = true;
        }
    }

    void onPermission(const QString &key, const QString &title,
                      const QJsonArray &options)
    {
        if (title != QStringLiteral("Start GTO solve")) {
            fail(QStringLiteral("unexpected permission title"));
            return;
        }
        for (const QJsonValue &value : options) {
            const QJsonObject option = value.toObject();
            if (option.value(QStringLiteral("kind")).toString()
                == QStringLiteral("allow_once")) {
                m_sawPermission = true;
                m_client.respondPermission(
                    key, option.value(QStringLiteral("optionId")).toString());
                return;
            }
        }
        fail(QStringLiteral("permission options did not include allow_once"));
    }

    void onPromptFinished(const QString &stopReason)
    {
        if (stopReason != QStringLiteral("end_turn")
            || !m_sawText || !m_sawTool || !m_sawPermission) {
            fail(QStringLiteral("ACP flow did not complete as expected"));
            return;
        }
        m_client.stop();
        QCoreApplication::exit(0);
    }

    void fail(const QString &message)
    {
        qCritical().noquote() << message;
        m_client.stop();
        QCoreApplication::exit(1);
    }

private:
    AcpClient m_client;
    bool m_sawText = false;
    bool m_sawTool = false;
    bool m_sawPermission = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    ChatSmokeTest test;
    return application.exec();
}

#include "chat_smoke_test.moc"
