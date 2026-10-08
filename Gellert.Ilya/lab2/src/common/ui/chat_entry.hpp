#pragma once

#include <QMetaType>
#include <QString>

namespace chat::ui {

/// One line of a conversation as the window shows it.
struct ChatEntry {
    enum class Kind {
        Message, ///< a bubble
        Status,  ///< a small centred note: someone joined, left, the connection broke...
    };

    Kind kind = Kind::Message;
    qint64 timeMs = 0; ///< the sender's time; a conversation is sorted by it
    quint32 authorId = 0;
    QString author;
    QString text;
    bool own = false; ///< written by this window's user: drawn on the right

    /// Forwarded by the host: the original author (forwardedFromId) and their name.
    quint32 forwardedFromId = 0xFFFF'FFFE;
    QString forwardedFrom;
};

} // namespace chat::ui

Q_DECLARE_METATYPE(chat::ui::ChatEntry)
