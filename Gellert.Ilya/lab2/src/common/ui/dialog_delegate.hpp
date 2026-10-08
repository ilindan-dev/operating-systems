#pragma once

#include <QStyledItemDelegate>

namespace chat::ui {

/// Data roles of an item in the dialog list.
namespace dialog_role {
inline constexpr int kKey = Qt::UserRole + 1;         ///< quint64: which conversation
inline constexpr int kParticipant = Qt::UserRole + 2; ///< quint32: who (kBroadcast = common chat)
inline constexpr int kPreview = Qt::UserRole + 3; ///< QString: the last line of the conversation
inline constexpr int kTime = Qt::UserRole + 4;    ///< qint64: when it was written, ms
inline constexpr int kUnread = Qt::UserRole + 5;  ///< int: messages not seen yet
inline constexpr int kOnline = Qt::UserRole + 6;  ///< bool: still in the chat
} // namespace dialog_role

/// Draws a dialog like a messenger does: avatar, name, last message, time and unread badge.
class DialogDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
};

} // namespace chat::ui
