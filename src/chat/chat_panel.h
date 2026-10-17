#ifndef RANGEPILOT_CHAT_PANEL_H
#define RANGEPILOT_CHAT_PANEL_H

#include "acp_client.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace RangePilot {

class ChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPanel(QWidget *parent = nullptr);
    ~ChatPanel() override;

    void startSession(const AcpClientConfig &config);
    void shutdown();

private slots:
    void onSendClicked();
    void onStopClicked();
    void onClientError(const QString &message);
    void onInitialized();
    void onSessionReady(const QString &sessionId);
    void onAssistantText(const QString &text);
    void onAgentThought(const QString &text);
    void onToolStatusChanged(const QString &toolCallId,
                             const QString &title,
                             const QString &status);
    void onPermissionRequested(const QString &requestKey,
                               const QString &title,
                               const QJsonArray &options);
    void onPromptFinished(const QString &stopReason);
    void onBusyChanged(bool busy);
    void onProcessStopped(int exitCode, const QString &stderrTail);

private:
    void appendBlock(const QString &role, const QString &text);
    void appendAssistantDelta(const QString &text);
    void appendNotice(const QString &text);
    void setStatus(const QString &text);
    void updateControls();

    AcpClient *m_client = nullptr;
    QTextBrowser *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_toolStatusLabel = nullptr;
    bool m_ready = false;
    bool m_assistantOpen = false;
};

} // namespace RangePilot

#endif // RANGEPILOT_CHAT_PANEL_H
