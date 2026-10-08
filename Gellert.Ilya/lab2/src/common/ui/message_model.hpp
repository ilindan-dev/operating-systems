#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "ui/chat_entry.hpp"

namespace chat::ui {

/// The chat history, always ordered by the time messages were sent.
class MessageModel : public QAbstractListModel {
    Q_OBJECT

public:
    static constexpr int kEntryRole = Qt::UserRole + 1;

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    /// Inserts by time (after entries with the same time) and returns the row.
    int add(ChatEntry entry);

private:
    QVector<ChatEntry> entries_;
};

} // namespace chat::ui
