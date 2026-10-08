#include "message_model.hpp"

#include <algorithm>

namespace chat::ui {

int MessageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant MessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= entries_.size()) {
        return {};
    }
    const ChatEntry& entry = entries_[index.row()];
    switch (role) {
    case kEntryRole:
        return QVariant::fromValue(entry);
    case Qt::DisplayRole:
        return entry.text;
    default:
        return {};
    }
}

int MessageModel::add(ChatEntry entry)
{
    // Messages from different clients can arrive slightly out of order: keep them sorted.
    const auto position =
        std::upper_bound(entries_.begin(), entries_.end(), entry.timeMs,
                         [](qint64 time, const ChatEntry& e) { return time < e.timeMs; });
    const int row = static_cast<int>(position - entries_.begin());
    beginInsertRows({}, row, row);
    entries_.insert(row, std::move(entry));
    endInsertRows();
    return row;
}

} // namespace chat::ui
