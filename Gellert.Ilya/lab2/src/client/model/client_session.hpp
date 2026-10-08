#pragma once

#include <sys/types.h>

#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "proto/message.hpp"
#include "transport/conn.hpp"

namespace chat::client {

/// What the client's window learns from the session. Called from the reader thread.
class ClientListener {
public:
    virtual ~ClientListener() = default;
    virtual void onJoined(proto::ParticipantId id, const std::string& name) = 0;
    virtual void onLeft(proto::ParticipantId id, const std::string& reason) = 0;
    virtual void onMessage(const proto::Message& message) = 0;
    /// The host is gone or the channel broke; the session is over.
    virtual void onDisconnected(const std::string& reason) = 0;
};

/**
 * A client's connection to the chat: handshake, the channel and a thread reading from it.
 */
class ClientSession {
public:
    ClientSession(ClientListener& listener, pid_t hostPid, std::string name);
    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    /// Handshake, open the channel, say hello, start reading.
    /// Throws std::runtime_error with a message for the user.
    void connect();
    void stop();

    /// Sends a chat message to `to` (kBroadcast, kHostId or a participant). False if disconnected.
    bool send(proto::ParticipantId to, std::string text);

    [[nodiscard]] proto::ParticipantId id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] pid_t hostPid() const noexcept;

private:
    bool write(const proto::Message& message);
    void readLoop(const std::stop_token& stop);

    ClientListener& listener_;
    pid_t hostPid_;
    std::string name_;
    proto::ParticipantId id_ = 0;

    std::optional<transport::Conn> conn_;
    std::mutex writeMutex_;
    std::jthread reader_; ///< declared last: joined before the channel is closed
};

} // namespace chat::client
