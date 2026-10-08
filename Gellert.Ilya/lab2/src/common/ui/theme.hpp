#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

class QApplication;

namespace chat::ui {

/// Colours of the dark theme, used by both the stylesheet and the custom painting.
namespace palette {
inline const QColor background{0x0E, 0x11, 0x17};
inline const QColor surface{0x14, 0x19, 0x22};
inline const QColor raised{0x1C, 0x23, 0x31};
inline const QColor border{0x23, 0x2B, 0x3A};
inline const QColor text{0xE7, 0xEA, 0xF0};
inline const QColor muted{0x8B, 0x95, 0xA7};
inline const QColor accent{0x5B, 0x8C, 0xFF};
inline const QColor accentSecond{0x8A, 0x5C, 0xFF};
inline const QColor bubble{0x1E, 0x25, 0x32};
inline const QColor online{0x3D, 0xD6, 0x8C};
inline const QColor offline{0xFF, 0x5C, 0x7A};
} // namespace palette

/// Fusion style, dark palette and the application stylesheet.
void applyTheme(QApplication& app);

/// A stable colour per participant.
[[nodiscard]] QColor avatarColor(quint32 id);

/// A round avatar with the first letter of `name`.
[[nodiscard]] QPixmap avatar(const QString& name, quint32 id, int size);

/// The avatar of the common chat.
[[nodiscard]] QPixmap commonChatAvatar(int size);

} // namespace chat::ui
