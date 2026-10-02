#include "daemon.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <system_error>

#include "pid_manager.hpp"

namespace monitor {

namespace {

[[noreturn]] void throwErrno(const char* what)
{
    throw std::system_error(errno, std::generic_category(), what);
}

/// Classic double-fork daemonization: fork, setsid, fork, umask, chdir("/") and
/// reopening descriptors 0, 1, 2 on /dev/null.
void daemonize()
{
    if (const pid_t pid = fork(); pid < 0) {
        throwErrno("First fork failed");
    } else if (pid > 0) {
        _exit(EXIT_SUCCESS); // no destructors and no stdio flush in the parent
    }

    if (setsid() < 0) {
        throwErrno("setsid failed");
    }

    // The session leader exits, so the daemon can never reacquire a controlling terminal.
    if (const pid_t pid = fork(); pid < 0) {
        throwErrno("Second fork failed");
    } else if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    umask(027);
    if (chdir("/") < 0) {
        throwErrno("chdir to / failed");
    }

    // Closing 0-2 is not enough: the next open() would reuse them, and stray writes to
    // stdout/stderr would land in that file.
    const int devNull = open("/dev/null", O_RDWR);
    if (devNull < 0) {
        throwErrno("open /dev/null failed");
    }
    for (const int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (dup2(devNull, fd) < 0) {
            throwErrno("dup2 failed");
        }
    }
    if (devNull > STDERR_FILENO) {
        close(devNull);
    }
}

} // namespace

Daemon& Daemon::getInstance()
{
    static Daemon instance;
    return instance;
}

void Daemon::init(const Config& config, const std::filesystem::path& pidFilePath)
{
    config_ = config;
    pidFilePath_ = std::filesystem::absolute(pidFilePath);
}

void Daemon::setupSignals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);

    // Blocked signals are not delivered asynchronously; they are read from the signalfd.
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        throwErrno("sigprocmask failed");
    }
    signalFd_ = signalfd(-1, &mask, SFD_CLOEXEC);
    if (signalFd_ < 0) {
        throwErrno("signalfd failed");
    }
}

void Daemon::run()
{
    PidManager::terminateRunningInstance(pidFilePath_);

    daemonize();
    setupSignals();

    try {
        PidManager::writePidFile(pidFilePath_);
        syslog(LOG_INFO, "Daemon started (PID %d), config: %s", getpid(),
               config_.cfgAbsPath.c_str());

        worker_.start(config_.directories);

        running_ = true;
        eventLoop();
    } catch (...) {
        shutdown();
        throw;
    }
    shutdown();
}

void Daemon::eventLoop()
{
    std::array<pollfd, 2> fds{{
        {signalFd_, POLLIN, 0},
        {worker_.fd(), POLLIN, 0},
    }};

    while (running_) {
        if (poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwErrno("poll failed");
        }
        if ((fds[0].revents & POLLIN) != 0) {
            handleSignal();
        }
        if (running_ && (fds[1].revents & POLLIN) != 0) {
            worker_.processEvents();
        }
    }
}

void Daemon::handleSignal()
{
    signalfd_siginfo info{};
    if (read(signalFd_, &info, sizeof(info)) != static_cast<ssize_t>(sizeof(info))) {
        return;
    }

    switch (info.ssi_signo) {
    case SIGHUP:
        reloadConfig();
        break;
    case SIGTERM:
    case SIGINT:
        syslog(LOG_INFO, "Received %s, shutting down",
               info.ssi_signo == SIGTERM ? "SIGTERM" : "SIGINT");
        running_ = false;
        break;
    default:
        break;
    }
}

void Daemon::reloadConfig()
{
    syslog(LOG_INFO, "Received SIGHUP, reloading configuration from %s",
           config_.cfgAbsPath.c_str());
    try {
        config_ = ConfigParser::parse(config_.cfgAbsPath);
        worker_.reload(config_.directories);
        syslog(LOG_INFO, "Configuration reloaded successfully");
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Failed to reload configuration: %s. Keeping the previous one.", e.what());
    }
}

void Daemon::shutdown()
{
    running_ = false;
    worker_.stop();
    if (signalFd_ >= 0) {
        close(signalFd_);
        signalFd_ = -1;
    }
    PidManager::removePidFileIfOwned(pidFilePath_);
    syslog(LOG_INFO, "Daemon stopped");
}

} // namespace monitor
