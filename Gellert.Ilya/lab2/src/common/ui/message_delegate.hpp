#pragma once

#include <QStyledItemDelegate>

class QListView;

namespace chat::ui {

struct ChatEntry;

/// Draws chat entries as bubbles (messages) and centred notes (statuses).
class MessageDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit MessageDelegate(QListView* view);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

private:
    struct Layout;
    [[nodiscard]] static Layout layout(const ChatEntry& entry, const QFont& base, int width);
    [[nodiscard]] int viewWidth() const;

    QListView* view_;
};

} // namespace chat::ui
