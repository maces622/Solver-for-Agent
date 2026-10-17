#include "chat_panel.h"

#include <QAbstractButton>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCharFormat>

#include <memory>
#include <QTextCursor>
#include <QVBoxLayout>

namespace RangePilot {

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
    , m_client(new AcpClient(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("<b>GTO Agent Chat</b>"), this);
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"), this);
    m_statusLabel->setWordWrap(true);
    m_toolStatusLabel = new QLabel(QStringLiteral("No active tool call"), this);
    m_toolStatusLabel->setWordWrap(true);
    m_toolStatusLabel->setStyleSheet(QStringLiteral("color: #555;"));

    m_transcript = new QTextBrowser(this);
    m_transcript->setOpenExternalLinks(false);
    m_transcript->setMinimumHeight(240);
    m_transcript->setFont(QFont(QStringLiteral("Segoe UI"), 10));

    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Ask the GTO agent..."));
    m_input->setMaximumHeight(110);

    m_sendButton = new QPushButton(QStringLiteral("Send"), this);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), this);
    m_stopButton->setEnabled(false);

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(m_sendButton);
    buttons->addWidget(m_stopButton);
    buttons->addStretch(1);

    layout->addWidget(title);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_toolStatusLabel);
    layout->addWidget(m_transcript, 1);
    layout->addWidget(m_input);
    layout->addLayout(buttons);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatPanel::onSendClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &ChatPanel::onStopClicked);
    connect(m_client, &AcpClient::errorOccurred, this, &ChatPanel::onClientError);
    connect(m_client, &AcpClient::initialized, this, &ChatPanel::onInitialized);
    connect(m_client, &AcpClient::sessionReady, this, &ChatPanel::onSessionReady);
    connect(m_client, &AcpClient::assistantText, this, &ChatPanel::onAssistantText);
    connect(m_client, &AcpClient::agentThought, this, &ChatPanel::onAgentThought);
    connect(m_client, &AcpClient::toolStatusChanged,
            this, &ChatPanel::onToolStatusChanged);
    connect(m_client, &AcpClient::permissionRequested,
            this, &ChatPanel::onPermissionRequested);
    connect(m_client, &AcpClient::promptFinished,
            this, &ChatPanel::onPromptFinished);
    connect(m_client, &AcpClient::busyChanged,
            this, &ChatPanel::onBusyChanged);
    connect(m_client, &AcpClient::processStopped,
            this, &ChatPanel::onProcessStopped);

    appendNotice(QStringLiteral(
        "Chat is not connected. Start RangePilot with "
        "RANGEPILOT_DSH_COMMAND/RANGEPILOT_DSH_ARGS or open settings to configure DSH."));
    updateControls();
}

ChatPanel::~ChatPanel()
{
    shutdown();
}

void ChatPanel::startSession(const AcpClientConfig &config)
{
    m_ready = false;
    m_assistantOpen = false;
    setStatus(QStringLiteral("Starting DSH ACP..."));
    appendNotice(QStringLiteral("Starting DSH ACP profile."));
    updateControls();
    m_client->start(config);
}

void ChatPanel::shutdown()
{
    m_ready = false;
    m_client->stop();
    updateControls();
}

void ChatPanel::onSendClicked()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (!m_ready) {
        appendNotice(QStringLiteral("DSH ACP session is not ready."));
        return;
    }
    appendBlock(QStringLiteral("You"), text);
    m_input->clear();
    m_assistantOpen = false;
    m_client->prompt(text);
}

void ChatPanel::onStopClicked()
{
    if (m_client->isBusy()) {
        m_client->cancelPrompt();
        m_toolStatusLabel->setText(QStringLiteral("Cancellation requested"));
    }
}

void ChatPanel::onClientError(const QString &message)
{
    appendNotice(QStringLiteral("Error: %1").arg(message));
    setStatus(QStringLiteral("ACP error"));
    updateControls();
}

void ChatPanel::onInitialized()
{
    setStatus(QStringLiteral("ACP initialized; creating session..."));
    updateControls();
}

void ChatPanel::onSessionReady(const QString &sessionId)
{
    m_ready = true;
    setStatus(QStringLiteral("Connected; session %1").arg(sessionId));
    appendNotice(QStringLiteral("Agent session is ready."));
    updateControls();
}

void ChatPanel::onAssistantText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    appendAssistantDelta(text);
}

void ChatPanel::onAgentThought(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_toolStatusLabel->setText(QStringLiteral("Thinking: %1").arg(text.left(160)));
}

void ChatPanel::onToolStatusChanged(const QString &toolCallId,
                                    const QString &title,
                                    const QString &status)
{
    Q_UNUSED(toolCallId)
    m_toolStatusLabel->setText(
        QStringLiteral("%1 [%2]").arg(title.isEmpty() ? QStringLiteral("Tool call") : title,
                                     status.isEmpty() ? QStringLiteral("running") : status));
}

void ChatPanel::onPermissionRequested(const QString &requestKey,
                                      const QString &title,
                                      const QJsonArray &options)
{
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::WindowModal);
    box->setWindowTitle(QStringLiteral("GTO Solve Approval"));
    box->setIcon(QMessageBox::Question);
    box->setText(title.isEmpty()
                     ? QStringLiteral("The agent requests approval for a GTO solve.")
                     : title);
    box->setInformativeText(QStringLiteral(
        "Review the solve budget and spot in the chat context. "
        "Approval applies to this request only."));

    auto optionIds = std::make_shared<QHash<QAbstractButton *, QString>>();
    for (const QJsonValue &value : options) {
        const QJsonObject option = value.toObject();
        const QString optionId = option.value(QStringLiteral("optionId")).toString();
        const QString name = option.value(QStringLiteral("name")).toString(optionId);
        const QString kind = option.value(QStringLiteral("kind")).toString();
        if (optionId.isEmpty()) {
            continue;
        }
        const bool allow = kind.startsWith(QStringLiteral("allow"));
        QAbstractButton *button = box->addButton(
            name, allow ? QMessageBox::AcceptRole : QMessageBox::RejectRole);
        optionIds->insert(button, optionId);
    }
    box->addButton(QMessageBox::Cancel);

    auto resolved = std::make_shared<bool>(false);
    connect(box, &QMessageBox::buttonClicked, this,
            [this, requestKey, optionIds, resolved](QAbstractButton *clicked) {
        if (*resolved) {
            return;
        }
        *resolved = true;
        const auto it = optionIds->constFind(clicked);
        if (it == optionIds->constEnd()) {
            m_client->respondPermission(requestKey, QString(), true);
            appendNotice(QStringLiteral("Solve approval cancelled."));
        } else {
            m_client->respondPermission(requestKey, it.value(), false);
            appendNotice(QStringLiteral("Approval response sent: %1").arg(it.value()));
        }
        m_toolStatusLabel->setText(QStringLiteral("Approval decision sent"));
    });
    connect(box, &QDialog::rejected, this,
            [this, requestKey, resolved]() {
        if (*resolved) {
            return;
        }
        *resolved = true;
        m_client->respondPermission(requestKey, QString(), true);
        appendNotice(QStringLiteral("Solve approval cancelled."));
        m_toolStatusLabel->setText(QStringLiteral("Approval decision sent"));
    });
    box->open();
}

void ChatPanel::onPromptFinished(const QString &stopReason)
{
    m_assistantOpen = false;
    m_toolStatusLabel->setText(QStringLiteral("No active tool call"));
    setStatus(QStringLiteral("Connected; last turn: %1").arg(stopReason));
    updateControls();
}

void ChatPanel::onBusyChanged(bool busy)
{
    m_sendButton->setEnabled(m_ready && !busy);
    m_stopButton->setEnabled(m_ready && busy);
}

void ChatPanel::onProcessStopped(int exitCode, const QString &stderrTail)
{
    m_ready = false;
    setStatus(QStringLiteral("DSH ACP stopped (exit %1)").arg(exitCode));
    if (!stderrTail.trimmed().isEmpty()) {
        appendNotice(QStringLiteral("DSH stderr (tail):\n%1")
                         .arg(stderrTail.right(1200)));
    }
    updateControls();
}

void ChatPanel::appendBlock(const QString &role, const QString &text)
{
    QTextCursor cursor(m_transcript->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_transcript->document()->isEmpty()) {
        cursor.insertBlock();
    }
    cursor.insertText(role + QStringLiteral(": "), QTextCharFormat());
    cursor.insertText(QStringLiteral("\n") + text);
    m_transcript->moveCursor(QTextCursor::End);
}

void ChatPanel::appendAssistantDelta(const QString &text)
{
    QTextCursor cursor(m_transcript->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_assistantOpen) {
        if (!m_transcript->document()->isEmpty()) {
            cursor.insertBlock();
        }
        QTextCharFormat roleFormat;
        roleFormat.setFontWeight(QFont::Bold);
        cursor.insertText(QStringLiteral("Agent:"), roleFormat);
        cursor.insertBlock();
        m_assistantOpen = true;
    }
    cursor.insertText(text);
    m_transcript->moveCursor(QTextCursor::End);
}

void ChatPanel::appendNotice(const QString &text)
{
    QTextCursor cursor(m_transcript->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_transcript->document()->isEmpty()) {
        cursor.insertBlock();
    }
    QTextCharFormat format;
    format.setForeground(QColor(QStringLiteral("#666666")));
    format.setFontItalic(true);
    cursor.insertText(text, format);
    m_transcript->moveCursor(QTextCursor::End);
}

void ChatPanel::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void ChatPanel::updateControls()
{
    m_sendButton->setEnabled(m_ready && !m_client->isBusy());
    m_stopButton->setEnabled(m_ready && m_client->isBusy());
    m_input->setEnabled(m_ready);
}

} // namespace RangePilot
