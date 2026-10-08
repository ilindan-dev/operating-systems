#include "message_delegate.hpp"

#include <QDateTime>
#include <QFontMetrics>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "ui/chat_entry.hpp"
#include "ui/message_model.hpp"
#include "ui/theme.hpp"

namespace chat::ui {

namespace {

constexpr int kSideMargin = 18;
constexpr int kRowGap = 5;
constexpr int kAvatarSize = 32;
constexpr int kAvatarGap = 10;
constexpr int kPaddingX = 14;
constexpr int kPaddingY = 9;
constexpr int kHeaderGap = 3;
constexpr int kMaxBubbleWidth = 560;
constexpr int kRadius = 16;
constexpr int kStatusPaddingX = 14;
constexpr int kStatusPaddingY = 5;

QFont scaled(const QFont& base, qreal factor, bool bold = false)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * factor);
    font.setBold(bold);
    return font;
}

QString timeText(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"));
}

QString forwardedText(const ChatEntry& entry)
{
    return QStringLiteral("↪ Переслано · автор: %1").arg(entry.forwardedFrom);
}

/// Word wrap, but break very long words too instead of letting them overflow the bubble.
int wrapFlags(const QFontMetrics& metrics, const QString& text, int width)
{
    const auto words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool hasLongWord = std::ranges::any_of(
        words, [&](const QString& word) { return metrics.horizontalAdvance(word) > width; });
    return Qt::TextWordWrap | (hasLongWord ? Qt::TextWrapAnywhere : 0);
}

} // namespace

struct MessageDelegate::Layout {
    QRect bubble;
    QRect avatar;
    QRect header;
    QRect forwarded; ///< "↪ Переслано от …"; empty if the message was not forwarded
    QRect text;
    int textFlags = 0;
    int height = 0;
    QFont nameFont;
    QFont textFont;
    QFont smallFont;
};

MessageDelegate::MessageDelegate(QListView* view) : QStyledItemDelegate(view), view_(view) {}

int MessageDelegate::viewWidth() const
{
    const int width = view_->viewport()->width();
    return width > 0 ? width : 700;
}

MessageDelegate::Layout MessageDelegate::layout(const ChatEntry& entry, const QFont& base,
                                                int width)
{
    Layout result;
    result.nameFont = scaled(base, 0.92, true);
    result.textFont = scaled(base, 1.05);
    result.smallFont = scaled(base, 0.82);
    const QFontMetrics smallMetrics(result.smallFont);

    if (entry.kind == ChatEntry::Kind::Status) {
        const int maxText = std::max(100, width * 4 / 5 - 2 * kStatusPaddingX);
        result.textFlags = Qt::AlignCenter | wrapFlags(smallMetrics, entry.text, maxText);
        const QRect text =
            smallMetrics.boundingRect(QRect(0, 0, maxText, 10'000), result.textFlags, entry.text);
        const int pillWidth = text.width() + 2 * kStatusPaddingX;
        const int pillHeight = text.height() + 2 * kStatusPaddingY;
        result.bubble = QRect((width - pillWidth) / 2, kRowGap + 4, pillWidth, pillHeight);
        result.text = result.bubble.adjusted(kStatusPaddingX, kStatusPaddingY, -kStatusPaddingX,
                                             -kStatusPaddingY);
        result.height = pillHeight + 2 * kRowGap + 8;
        return result;
    }

    const int avatarSpace = entry.own ? 0 : kAvatarSize + kAvatarGap;
    const int maxBubble =
        std::min(kMaxBubbleWidth, (width - 2 * kSideMargin - avatarSpace) * 3 / 4);
    const int maxText = std::max(60, maxBubble - 2 * kPaddingX);

    const QFontMetrics textMetrics(result.textFont);
    result.textFlags = wrapFlags(textMetrics, entry.text, maxText);
    const QRect text =
        textMetrics.boundingRect(QRect(0, 0, maxText, 100'000), result.textFlags, entry.text);

    const QFontMetrics nameMetrics(result.nameFont);
    const int headerWidth = nameMetrics.horizontalAdvance(entry.author) + 16 +
                            smallMetrics.horizontalAdvance(timeText(entry.timeMs));
    const int headerHeight = std::max(nameMetrics.height(), smallMetrics.height());
    const bool forwarded = !entry.forwardedFrom.isEmpty();
    const int forwardedWidth = forwarded ? smallMetrics.horizontalAdvance(forwardedText(entry)) : 0;
    const int forwardedHeight = forwarded ? smallMetrics.height() + kHeaderGap : 0;

    const int bubbleWidth = std::min(
        maxBubble, std::max({text.width(), headerWidth, forwardedWidth, 70}) + 2 * kPaddingX);
    const int bubbleHeight =
        kPaddingY + headerHeight + kHeaderGap + forwardedHeight + text.height() + kPaddingY;
    const int innerWidth = bubbleWidth - 2 * kPaddingX;

    const int x = entry.own ? width - kSideMargin - bubbleWidth : kSideMargin + avatarSpace;
    result.bubble = QRect(x, kRowGap, bubbleWidth, bubbleHeight);
    result.header = QRect(x + kPaddingX, kRowGap + kPaddingY, innerWidth, headerHeight);
    const int below = result.header.bottom() + 1 + kHeaderGap;
    if (forwarded) {
        result.forwarded = QRect(x + kPaddingX, below, innerWidth, smallMetrics.height());
    }
    result.text = QRect(x + kPaddingX, below + forwardedHeight, innerWidth, text.height());
    if (!entry.own) {
        result.avatar =
            QRect(kSideMargin, result.bubble.bottom() - kAvatarSize + 1, kAvatarSize, kAvatarSize);
    }
    result.height = bubbleHeight + 2 * kRowGap;
    return result;
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto entry = index.data(MessageModel::kEntryRole).value<ChatEntry>();
    const int width = viewWidth();
    return {width, layout(entry, option.font, width).height};
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    const auto entry = index.data(MessageModel::kEntryRole).value<ChatEntry>();
    const Layout geometry = layout(entry, option.font, option.rect.width());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(option.rect.topLeft());

    if (entry.kind == ChatEntry::Kind::Status) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::raised);
        painter->drawRoundedRect(geometry.bubble, geometry.bubble.height() / 2.0,
                                 geometry.bubble.height() / 2.0);
        painter->setFont(geometry.smallFont);
        painter->setPen(palette::muted);
        painter->drawText(geometry.text, geometry.textFlags, entry.text);
        painter->restore();
        return;
    }

    // Bubble: own messages get the accent gradient, forwarded ones a violet outline.
    QPainterPath path;
    path.addRoundedRect(geometry.bubble, kRadius, kRadius);
    if (entry.own) {
        QLinearGradient gradient(geometry.bubble.topLeft(), geometry.bubble.bottomRight());
        gradient.setColorAt(0, palette::accent);
        gradient.setColorAt(1, palette::accentSecond);
        painter->fillPath(path, gradient);
    } else {
        painter->fillPath(path, palette::bubble);
        if (!geometry.forwarded.isEmpty()) {
            painter->setPen(QPen(palette::accentSecond, 1.2));
            painter->drawPath(path);
        }
        painter->drawPixmap(geometry.avatar, avatar(entry.author, entry.authorId, kAvatarSize));
    }

    // Header: name on the left, time on the right.
    const QFontMetrics nameMetrics(geometry.nameFont);
    painter->setFont(geometry.nameFont);
    painter->setPen(entry.own ? QColor(255, 255, 255, 220)
                              : avatarColor(entry.authorId).lighter(150));
    painter->drawText(
        geometry.header, Qt::AlignLeft | Qt::AlignVCenter,
        nameMetrics.elidedText(entry.author, Qt::ElideRight, geometry.header.width() * 2 / 3));
    painter->setFont(geometry.smallFont);
    painter->setPen(entry.own ? QColor(255, 255, 255, 170) : palette::muted);
    painter->drawText(geometry.header, Qt::AlignRight | Qt::AlignVCenter, timeText(entry.timeMs));

    if (!geometry.forwarded.isEmpty()) {
        painter->setPen(entry.own ? QColor(255, 255, 255, 210) : QColor(0xB9, 0x9C, 0xFF));
        painter->drawText(
            geometry.forwarded, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(geometry.smallFont)
                .elidedText(forwardedText(entry), Qt::ElideRight, geometry.forwarded.width()));
    }

    painter->setFont(geometry.textFont);
    painter->setPen(entry.own ? QColor(Qt::white) : palette::text);
    painter->drawText(geometry.text, geometry.textFlags, entry.text);
    painter->restore();
}

} // namespace chat::ui
