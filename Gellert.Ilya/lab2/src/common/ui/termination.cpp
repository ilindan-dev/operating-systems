#include "termination.hpp"

#include <sys/signalfd.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QSocketNotifier>
#include <csignal>
#include <format>
#include <memory>

#include "posix/log.hpp"
#include "posix/unique_fd.hpp"

namespace chat::ui {

void quitOnTerminationSignals(QCoreApplication& app)
{
    sigset_t set;
    sigemptyset(&set);
    for (const int signal : {SIGINT, SIGTERM, SIGHUP}) {
        sigaddset(&set, signal);
    }
    auto fd = std::make_shared<posix::UniqueFd>(signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!fd->valid()) {
        log::warning("signalfd failed: SIGTERM will not close the chat cleanly");
        return;
    }

    auto* notifier = new QSocketNotifier(fd->get(), QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [fd, &app] {
        signalfd_siginfo info{};
        if (::read(fd->get(), &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))) {
            log::info(std::format("signal {} received, closing", info.ssi_signo));
            app.quit();
        }
    });
}

} // namespace chat::ui
