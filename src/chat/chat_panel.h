#ifndef RANGEPILOT_CHAT_PANEL_H
#define RANGEPILOT_CHAT_PANEL_H

#include "acp_client.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QTimer;

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
    enum class EntryKind {
        User,
        Assistant,
        Notice
    };

    struct ChatEntry
    {
        EntryKind kind = EntryKind::Notice;
        QString text;
    };

    void appendUserMessage(const QString &text);
    void appendAssistantDelta(const QString &text);
    void appendNotice(const QString &text);
    void renderTranscript();
    void scrollToBottom();
    void setStatus(const QString &text);
    void updateControls();

    QVector<ChatEntry> m_entries;
    QHash<QString, QString> m_toolTitles;
    QTimer *m_renderTimer = nullptr;

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
