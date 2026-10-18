#include "src/chat/chat_panel.h"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextBrowser>
#include <QTimer>

using namespace RangePilot;

class ChatPanelSmokeTest : public QObject
{
    Q_OBJECT

public:
    explicit ChatPanelSmokeTest(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_waitReady.setInterval(100);
        connect(&m_waitReady, &QTimer::timeout, this, &ChatPanelSmokeTest::trySend);
        m_waitReady.start();

        m_waitDone.setInterval(100);
        connect(&m_waitDone, &QTimer::timeout, this, &ChatPanelSmokeTest::tryFinish);
        m_waitDone.start();

        QTimer::singleShot(10000, this, &ChatPanelSmokeTest::failTimeout);
    }

    void start()
    {
        m_panel.resize(460, 720);
        m_panel.show();
        AcpClientConfig config;
        const QString realCommand = qEnvironmentVariable("GTO_ACP_COMMAND");
        if (!realCommand.isEmpty()) {
            config.executable = realCommand;
            config.arguments = QProcess::splitCommand(qEnvironmentVariable("GTO_ACP_ARGS"));
            config.workingDirectory = qEnvironmentVariable("GTO_ACP_CWD");
        } else {
            config.executable = qEnvironmentVariable("GTO_TEST_PYTHON");
            config.arguments = QStringList{
                qEnvironmentVariable("GTO_ACP_FAKE_SERVER"),
            };
            config.workingDirectory = QCoreApplication::applicationDirPath();
            config.environment.insert(QStringLiteral("FAKE_ACP_PERMISSION"), QStringLiteral("0"));
        }
        if (config.executable.isEmpty()
            || (!config.arguments.isEmpty() && config.arguments.first().isEmpty())) {
            fail(QStringLiteral("test environment is incomplete"));
            return;
        }
        m_panel.startSession(config);
    }

private slots:
    void trySend()
    {
        if (m_sent) {
            return;
        }
        for (QLabel *label : m_panel.findChildren<QLabel *>()) {
            if (label->text().contains(QStringLiteral("Connected; session"))) {
                auto *input = m_panel.findChild<QPlainTextEdit *>();
                QPushButton *send = nullptr;
                for (QPushButton *button : m_panel.findChildren<QPushButton *>()) {
                    if (button->text() == QStringLiteral("Send")) {
                        send = button;
                    }
                }
                if (input && send) {
                    const QString prompt = qEnvironmentVariable(
                        "GTO_CHAT_PROMPT", QStringLiteral("test prompt"));
                    input->setPlainText(prompt);
                    send->click();
                    m_sent = true;
                    m_waitReady.stop();
                }
                return;
            }
        }
    }

    void tryFinish()
    {
        auto *transcript = m_panel.findChild<QTextBrowser *>();
        if (!transcript || !m_sent) {
            return;
        }
        const QString text = transcript->toPlainText();
        const QString expected = qEnvironmentVariable(
            "GTO_CHAT_EXPECT_TEXT", QStringLiteral("Hello from fake ACP"));
        if (!text.contains(expected)) {
            return;
        }
        const QString screenshot = qEnvironmentVariable("GTO_CHAT_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            m_panel.grab().save(screenshot);
        }
        m_panel.shutdown();
        qInfo().noquote() << QStringLiteral("chat_panel_smoke=ok");
        QCoreApplication::exit(0);
    }

    void failTimeout()
    {
        fail(QStringLiteral("Chat panel smoke test timed out"));
    }

    void fail(const QString &message)
    {
        qCritical().noquote() << message;
        m_panel.shutdown();
        QCoreApplication::exit(1);
    }

private:
    ChatPanel m_panel;
    QTimer m_waitReady;
    QTimer m_waitDone;
    bool m_sent = false;
};

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    ChatPanelSmokeTest test;
    QTimer::singleShot(0, &test, &ChatPanelSmokeTest::start);
    return application.exec();
}

#include "chat_panel_smoke_test.moc"
