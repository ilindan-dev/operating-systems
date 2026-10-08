// client_<type>: a chat participant. Usage: ./client_<type> <host pid> [name]
#include <unistd.h>

#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <charconv>
#include <csignal>
#include <cstring>
#include <format>
#include <optional>

#include "client/view/client_window.hpp"
#include "posix/io.hpp"
#include "posix/log.hpp"
#include "posix/signals.hpp"
#include "transport/conn.hpp"
#include "ui/termination.hpp"
#include "ui/theme.hpp"

namespace {

std::optional<pid_t> parsePid(const char* text)
{
    pid_t pid = 0;
    const char* end = text + std::strlen(text);
    const auto [ptr, error] = std::from_chars(text, end, pid);
    if (error != std::errc{} || ptr != end || pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

} // namespace

int main(int argc, char* argv[])
{
    using namespace chat;
    log::setRole(std::format("client/{}", transport::Conn::typeName()));
    posix::ignoreSigpipe();
    // Before Qt starts threads; SIGUSR1 is for the handshake, the rest end the GUI loop.
    const posix::SignalBlocker blocker{proto::kHandshakeSignal, SIGINT, SIGTERM, SIGHUP};

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LocalChat"));
    ui::applyTheme(app);
    ui::quitOnTerminationSignals(app);

    const auto hostPid = argc > 1 ? parsePid(argv[1]) : std::nullopt;
    if (!hostPid) {
        const QString usage = QStringLiteral("Использование: %1 <PID хоста> [имя]")
                                  .arg(QString::fromLocal8Bit(argv[0]));
        log::error(usage.toStdString());
        QMessageBox::warning(nullptr, QStringLiteral("LocalChat"), usage);
        return 2;
    }

    QString name = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (name.trimmed().isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(nullptr, QStringLiteral("LocalChat"),
                                     QStringLiteral("Как вас зовут?"), QLineEdit::Normal,
                                     QStringLiteral("Гость %1").arg(getpid()), &ok);
        if (!ok) {
            return 0;
        }
    }

    client::ClientWindow window(*hostPid, name.trimmed());
    if (const auto error = window.connect()) {
        log::error(error->toStdString());
        QMessageBox::critical(nullptr, QStringLiteral("Не удалось подключиться"), *error);
        return 1;
    }
    window.show();
    return QApplication::exec();
}
