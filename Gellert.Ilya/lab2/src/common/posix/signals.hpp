#pragma once

#include <sys/types.h>

#include <chrono>
#include <csignal>
#include <initializer_list>
#include <optional>

namespace chat::posix {

/// What the receiver learns about a signal taken with waitSignal().
struct SignalInfo {
    int signal = 0;      ///< signal number, e.g. SIGUSR1
    pid_t senderPid = 0; ///< who sent it (filled in by the kernel, the sender cannot fake it)
    int value = 0;       ///< integer payload passed to sendSignal()
};

/**
 * Blocks the given signals in the calling thread while the object lives,
 * then restores the previous mask.
 *
 * A blocked signal is not delivered (so its default action, e.g. "terminate" for SIGUSR1,
 * does not happen): it stays pending until waitSignal() takes it.
 * Threads inherit the mask, so block before creating threads.
 */
class SignalBlocker {
public:
    explicit SignalBlocker(std::initializer_list<int> signalList);
    ~SignalBlocker();

    SignalBlocker(const SignalBlocker&) = delete;
    SignalBlocker& operator=(const SignalBlocker&) = delete;

private:
    sigset_t previous_{};
};

/// Sends `signal` with an integer payload to process `pid`. Throws std::system_error on failure.
void sendSignal(pid_t pid, int signal, int value = 0);

/**
 * Waits until one of `signalList` arrives, at most `timeout`.
 * The signals must be blocked (see SignalBlocker), otherwise they are delivered instead.
 * Returns std::nullopt on timeout; throws std::system_error on other errors.
 */
[[nodiscard]] std::optional<SignalInfo> waitSignal(std::initializer_list<int> signalList,
                                                   std::chrono::milliseconds timeout);

} // namespace chat::posix
