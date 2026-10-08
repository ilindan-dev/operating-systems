#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <map>

#include "proto/constants.hpp"
#include "ui/chat_entry.hpp"

class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace chat::ui {

class MessageModel;

struct ChatWindowConfig {
    QString windowTitle;
    QString selfName;
    quint32 selfId = 0;
    QString transport;       ///< "fifo", "sock" or "pipe"
    QString info;            ///< second line of the "you" card, e.g. the host pid
    QString hint;            ///< small print at the bottom of the sidebar
    bool canForward = false; ///< the host can forward messages (right click on a message)
};

/**
 * The chat window shared by the host and the clients, laid out like a messenger:
 * dialogs on the left (the common chat and a private dialog per participant), the selected
 * conversation and the input on the right. Every dialog has its own history.
 *
 * It only shows things and reports what the user wants (sendRequested, forwardRequested);
 * the host/client code decides what to do. Must be used from the GUI thread.
 */
class ChatWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ChatWindow(ChatWindowConfig config, QWidget* parent = nullptr);

    /// Updates "who am I" (a client learns its id only after the handshake).
    void setSelf(quint32 id, const QString& name, const QString& info);

    /// Puts a message into its dialog: the common chat, or the private dialog with the other party.
    void addChatMessage(quint32 from, quint32 to, qint64 sentAtMs, const QString& text,
                        quint32 forwardedFrom = proto::kNobody);
    /// A centred note in the common chat (and in the dialog with `about`, if there is one).
    void addStatus(const QString& text, quint32 about = proto::kNobody, qint64 timeMs = -1);

    void addParticipant(quint32 id, const QString& name, const QString& details);
    /// The participant left: their dialog stays (greyed out, read-only) with its history.
    void removeParticipant(quint32 id);

    void setConnected(bool connected, const QString& text);
    /// Opens the common chat (kBroadcast) or the dialog with a participant.
    void openDialog(quint32 participant);

    [[nodiscard]] QString nameOf(quint32 id) const;

signals:
    /// The user wants to send `text` to `to` (the common chat or one participant).
    void sendRequested(quint32 to, const QString& text);
    /// The host's user wants to re-send `text` written by `author` to `to`.
    void forwardRequested(quint32 author, const QString& text, quint32 to);

private:
    struct Dialog {
        quint64 key = 0;
        quint32 participant = proto::kBroadcast;
        QString name;
        bool online = true;
        int unread = 0;
        QString preview;
        qint64 lastTimeMs = 0;
        MessageModel* model = nullptr;
        QListWidgetItem* item = nullptr;
    };

    QWidget* buildSidebar();
    QWidget* buildConversation();
    Dialog& createDialog(quint32 participant, const QString& name);
    Dialog* activeDialogWith(quint32 participant);
    Dialog& currentDialog();
    void append(Dialog& dialog, ChatEntry entry, const QString& preview);
    static void refreshItem(const Dialog& dialog);
    void showCurrentDialog();
    void updateInput();
    void updateCounter();
    void showMessageMenu(const QPoint& position);
    void submit();

    ChatWindowConfig config_;
    QHash<quint32, QString> names_;
    bool connected_ = true;

    std::map<quint64, Dialog> dialogs_;     ///< key 0 is the common chat
    QHash<quint32, quint64> activeDialogs_; ///< participant id → their current dialog
    quint64 nextKey_ = 1;
    quint64 currentKey_ = 0;

    QListView* messages_ = nullptr;
    QListWidget* dialogList_ = nullptr;
    QLabel* selfAvatar_ = nullptr;
    QLabel* selfName_ = nullptr;
    QLabel* selfInfo_ = nullptr;
    QLabel* chatAvatar_ = nullptr;
    QLabel* chatTitle_ = nullptr;
    QLabel* chatSubtitle_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* send_ = nullptr;
    QLabel* connection_ = nullptr;
    QLabel* counter_ = nullptr;
};

} // namespace chat::ui
