#include "signals.hpp"

#include <cerrno>
#include <system_error>

namespace chat::posix {

namespace {

sigset_t makeSet(const std::initializer_list<int> signalList)
{
    sigset_t set;
    sigemptyset(&set);
    for (const int signal : signalList) {
        sigaddset(&set, signal);
    }
    return set;
}

timespec toTimespec(const std::chrono::milliseconds timeout)
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds);
    return timespec{seconds.count(), nanoseconds.count()};
}

} // namespace

SignalBlocker::SignalBlocker(const std::initializer_list<int> signalList)
{
    const sigset_t set = makeSet(signalList);
    if (const int error = pthread_sigmask(SIG_BLOCK, &set, &previous_); error != 0) {
        throw std::system_error(error, std::generic_category(), "pthread_sigmask");
    }
}

SignalBlocker::~SignalBlocker()
{
    pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
}

void sendSignal(const pid_t pid, const int signal, const int value)
{
    sigval payload{};
    payload.sival_int = value;
    if (sigqueue(pid, signal, payload) != 0) {
        throw std::system_error(errno, std::generic_category(), "sigqueue");
    }
}

std::optional<SignalInfo> waitSignal(const std::initializer_list<int> signalList,
                                     const std::chrono::milliseconds timeout)
{
    const sigset_t set = makeSet(signalList);
    const timespec ts = toTimespec(timeout);
    siginfo_t info{};
    while (true) {
        const int signal = sigtimedwait(&set, &info, &ts);
        if (signal > 0) {
            return SignalInfo{signal, info.si_pid, info.si_value.sival_int};
        }
        if (errno == EAGAIN) {
            return std::nullopt;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "sigtimedwait");
        }
    }
}

} // namespace chat::posix
