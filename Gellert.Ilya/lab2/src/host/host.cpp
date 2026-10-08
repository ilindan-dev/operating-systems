// host_<type>: the chat host. Start it first, then clients: ./client_<type> <host pid> [name]
#include <unistd.h>

#include <QApplication>
#include <QMessageBox>
#include <csignal>
#include <exception>
#include <format>

#include "host/view/host_window.hpp"
#include "posix/io.hpp"
#include "posix/log.hpp"
#include "posix/signals.hpp"
#include "transport/conn.hpp"
#include "ui/termination.hpp"
#include "ui/theme.hpp"

int main(int argc, char* argv[])
{
    using namespace chat;
    log::setRole(std::format("host/{}", transport::Conn::typeName()));
    posix::ignoreSigpipe();
    // Before Qt starts any threads: they all inherit the mask. SIGUSR1 is then taken only by
    // the handshake thread (sigtimedwait), the termination signals by the GUI loop (signalfd).
    const posix::SignalBlocker blocker{proto::kHandshakeSignal, SIGINT, SIGTERM, SIGHUP};

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LocalChat"));
    ui::applyTheme(app);
    ui::quitOnTerminationSignals(app);

    try {
        host::HostWindow window;
        window.show();
        log::info(std::format("host started; clients: ./client_{} {}", transport::Conn::typeName(),
                              getpid()));
        return QApplication::exec();
    } catch (const std::exception& e) {
        log::error(e.what());
        QMessageBox::critical(
            nullptr, QStringLiteral("LocalChat"),
            QStringLiteral("Не удалось запустить хост:\n%1").arg(QString::fromStdString(e.what())));
        return 1;
    }
}
