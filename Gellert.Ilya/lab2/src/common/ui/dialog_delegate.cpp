#include "dialog_delegate.hpp"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <algorithm>

#include "proto/constants.hpp"
#include "ui/theme.hpp"

namespace chat::ui {

namespace {

constexpr int kHeight = 62;
constexpr int kAvatarSize = 40;
constexpr int kPadding = 10;

QString shortTime(qint64 ms)
{
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"))
                  : QString();
}

} // namespace

QSize DialogDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return {option.rect.width(), kHeight};
}

void DialogDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const
{
    const QString name = index.data(Qt::DisplayRole).toString();
    const auto participant = index.data(dialog_role::kParticipant).toUInt();
    const QString preview = index.data(dialog_role::kPreview).toString();
    const QString time = shortTime(index.data(dialog_role::kTime).toLongLong());
    const int unread = index.data(dialog_role::kUnread).toInt();
    const bool online = index.data(dialog_role::kOnline).toBool();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect cell = option.rect.adjusted(0, 2, 0, -2);

    if ((option.state & QStyle::State_Selected) != 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x26, 0x32, 0x4D));
        painter->drawRoundedRect(cell, 12, 12);
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::raised);
        painter->drawRoundedRect(cell, 12, 12);
    }

    const QRect avatarRect(cell.left() + kPadding, cell.center().y() - kAvatarSize / 2 + 1,
                           kAvatarSize, kAvatarSize);
    painter->setOpacity(online ? 1.0 : 0.4);
    painter->drawPixmap(avatarRect, participant == proto::kBroadcast
                                        ? commonChatAvatar(kAvatarSize)
                                        : avatar(name, participant, kAvatarSize));
    painter->setOpacity(1.0);
    if (online && participant != proto::kBroadcast) {
        // small green "online" dot on the avatar
        painter->setPen(QPen(palette::surface, 2));
        painter->setBrush(palette::online);
        painter->drawEllipse(QPointF(avatarRect.right() - 4, avatarRect.bottom() - 4), 5, 5);
    }

    const int textLeft = avatarRect.right() + 12;
    const int textRight = cell.right() - kPadding;
    QFont nameFont = option.font;
    nameFont.setBold(true);
    QFont smallFont = option.font;
    smallFont.setPointSizeF(option.font.pointSizeF() * 0.85);
    const QFontMetrics smallMetrics(smallFont);

    // First line: name and time.
    const int timeWidth = time.isEmpty() ? 0 : smallMetrics.horizontalAdvance(time) + 8;
    const QRect nameRect(textLeft, cell.top() + 11, textRight - textLeft - timeWidth,
                         QFontMetrics(nameFont).height());
    painter->setFont(nameFont);
    painter->setPen(online ? palette::text : palette::muted);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, nameRect.width()));
    painter->setFont(smallFont);
    painter->setPen(palette::muted);
    painter->drawText(QRect(textLeft, nameRect.top(), textRight - textLeft, nameRect.height()),
                      Qt::AlignRight | Qt::AlignVCenter, time);

    // Second line: the last message and the unread badge.
    int previewRight = textRight;
    const int lineTop = nameRect.bottom() + 4;
    const int lineHeight = smallMetrics.height() + 2;
    if (unread > 0) {
        const QString count = unread > 99 ? QStringLiteral("99+") : QString::number(unread);
        const int badgeWidth = std::max(lineHeight, smallMetrics.horizontalAdvance(count) + 12);
        const QRect badge(textRight - badgeWidth, lineTop, badgeWidth, lineHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::accent);
        painter->drawRoundedRect(badge, lineHeight / 2.0, lineHeight / 2.0);
        QFont badgeFont = smallFont;
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, count);
        painter->setFont(smallFont);
        previewRight = badge.left() - 8;
    }
    painter->setPen(palette::muted);
    const QRect previewRect(textLeft, lineTop, previewRight - textLeft, lineHeight);
    painter->drawText(previewRect, Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(preview, Qt::ElideRight, previewRect.width()));
    painter->restore();
}

} // namespace chat::ui
