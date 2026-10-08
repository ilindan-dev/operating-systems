#include "theme.hpp"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>

namespace chat::ui {

namespace {

constexpr auto kStyleSheet = R"(
QWidget { color: #E7EAF0; }
QMainWindow, #central { background: #0E1117; }

#sidebar { background: #141922; border-right: 1px solid #232B3A; }
#brand { font-size: 16pt; font-weight: 700; letter-spacing: 0.5px; }
#badge {
    background: rgba(91, 140, 255, 0.16); color: #9DB8FF;
    border-radius: 9px; padding: 2px 9px; font-size: 8pt; font-weight: 700;
}
#selfCard { background: #1A2130; border: 1px solid #232B3A; border-radius: 14px; }
#selfName { font-size: 11pt; font-weight: 700; }
#selfInfo { color: #8B95A7; font-size: 8.5pt; }
#sectionLabel { color: #6F7A8E; font-size: 8pt; font-weight: 700; letter-spacing: 1.5px; }
#hint { color: #6F7A8E; font-size: 8pt; }

QListWidget#dialogs { background: transparent; border: none; outline: none; }

#topBar { background: #141922; border-bottom: 1px solid #232B3A; }
#chatTitle { font-size: 13pt; font-weight: 700; }
#chatSubtitle { color: #8B95A7; font-size: 9pt; }

QListView#messages { background: #0E1117; border: none; }

#composer { background: #141922; border-top: 1px solid #232B3A; }
QLineEdit#input {
    background: #1C2331; border: 1px solid #2A3345; border-radius: 19px;
    padding: 9px 16px; font-size: 10.5pt; selection-background-color: #5B8CFF;
}
QLineEdit#input:focus { border: 1px solid #5B8CFF; }
QLineEdit#input:disabled { color: #5D6678; }
QPushButton#send {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #5B8CFF, stop:1 #8A5CFF);
    border: none; border-radius: 19px; padding: 9px 22px; font-weight: 700; color: white;
}
QPushButton#send:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6E9AFF, stop:1 #9A70FF);
}
QPushButton#send:pressed { background: #4A73D9; }
QPushButton#send:disabled { background: #2A3345; color: #6B7487; }

QStatusBar { background: #141922; border-top: 1px solid #232B3A; color: #8B95A7; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #8B95A7; padding: 0 6px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }
QScrollBar::handle:vertical { background: #2A3345; border-radius: 3px; min-height: 36px; }
QScrollBar::handle:vertical:hover { background: #3A4560; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QMenu { background: #1C2331; border: 1px solid #2A3345; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 22px 7px 12px; border-radius: 6px; }
QMenu::item:selected { background: #26324D; }
QMenu::item:disabled { color: #5D6678; }
QMenu::separator { height: 1px; background: #2A3345; margin: 4px 8px; }
QToolTip { background: #1C2331; color: #E7EAF0; border: 1px solid #2A3345; padding: 4px 8px; }
QDialog, QMessageBox { background: #141922; }
QDialog QLineEdit {
    background: #1C2331; border: 1px solid #2A3345; border-radius: 8px; padding: 6px 10px;
}
QDialog QPushButton, QMessageBox QPushButton {
    background: #26324D; border: none; border-radius: 8px; padding: 6px 16px; min-width: 70px;
}
QDialog QPushButton:hover, QMessageBox QPushButton:hover { background: #31406A; }
)";

} // namespace

void applyTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window, palette::surface);
    dark.setColor(QPalette::WindowText, palette::text);
    dark.setColor(QPalette::Base, palette::raised);
    dark.setColor(QPalette::AlternateBase, palette::surface);
    dark.setColor(QPalette::Text, palette::text);
    dark.setColor(QPalette::Button, palette::raised);
    dark.setColor(QPalette::ButtonText, palette::text);
    dark.setColor(QPalette::Highlight, palette::accent);
    dark.setColor(QPalette::HighlightedText, Qt::white);
    dark.setColor(QPalette::ToolTipBase, palette::raised);
    dark.setColor(QPalette::ToolTipText, palette::text);
    dark.setColor(QPalette::PlaceholderText, palette::muted);
    dark.setColor(QPalette::Disabled, QPalette::Text, palette::muted);
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, palette::muted);
    app.setPalette(dark);

    QFont font = app.font();
    font.setPointSizeF(10);
    app.setFont(font);

    app.setStyleSheet(kStyleSheet);
}

QColor avatarColor(quint32 id)
{
    // Golden-angle steps give well separated hues for consecutive ids.
    const int hue = static_cast<int>((id * 137U + 210U) % 360U);
    return QColor::fromHsl(hue, 150, 120);
}

QPixmap avatar(const QString& name, quint32 id, int size)
{
    const qreal ratio = 2.0; // sharp on HiDPI screens
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor base = avatarColor(id);
    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0, base.lighter(125));
    gradient.setColorAt(1, base.darker(115));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QRectF(0, 0, size, size));

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(size * 2 / 5);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QString letter =
        name.trimmed().isEmpty() ? QStringLiteral("?") : name.trimmed().left(1).toUpper();
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter);
    return pixmap;
}

QPixmap commonChatAvatar(int size)
{
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0, palette::accent);
    gradient.setColorAt(1, palette::accentSecond);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QRectF(0, 0, size, size));

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(size / 2);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QStringLiteral("#"));
    return pixmap;
}

} // namespace chat::ui
