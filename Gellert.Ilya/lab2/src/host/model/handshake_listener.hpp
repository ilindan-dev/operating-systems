#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <thread>

namespace chat::host {

/**
 * Host side of the handshake ("двустороннее знакомство с помощью сигнала").
 *
 * A client sends SIGUSR1 to the host pid. This thread takes it with sigtimedwait, learns the
 * client pid from siginfo, asks `onKnock` to create a channel for it and answers with
 * SIGUSR1 carrying the client id (or -1 if the channel could not be created).
 *
 * SIGUSR1 must be blocked in every thread of the host before this one starts.
 */
class HandshakeListener {
public:
    /// Returns the id given to the client, or nothing to refuse it.
    using OnKnock = std::function<std::optional<std::uint32_t>(pid_t clientPid)>;

    explicit HandshakeListener(OnKnock onKnock);
    ~HandshakeListener();

    HandshakeListener(const HandshakeListener&) = delete;
    HandshakeListener& operator=(const HandshakeListener&) = delete;

    void start();
    void stop();

private:
    void run(const std::stop_token& stop) const;

    OnKnock onKnock_;
    std::jthread thread_;
};

} // namespace chat::host
