#pragma once

#include <sys/types.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "host/model/handshake_listener.hpp"
#include "host/model/participant.hpp"
#include "proto/constants.hpp"
#include "proto/message.hpp"

namespace chat::host {

/// What the host's window learns from the Hub. Called from the Hub's threads, not the GUI thread.
class HubListener {
public:
    virtual ~HubListener() = default;
    virtual void onJoined(proto::ParticipantId id, const std::string& name, pid_t pid) = 0;
    virtual void onLeft(proto::ParticipantId id, const std::string& name,
                        const std::string& reason) = 0;
    /// A message the host should display: everything public and private ones to/from the host.
    virtual void onMessage(const proto::Message& message) = 0;
};

struct HubOptions {
    std::string hostName = "Хост";
    std::chrono::milliseconds inactivityLimit = proto::kInactivityLimit;
};

struct ParticipantInfo {
    proto::ParticipantId id;
    std::string name;
    pid_t pid;
};

/**
 * The chat server inside the host process.
 *
 *  - HandshakeListener thread: accepts clients (SIGUSR1) and creates a Conn for each;
 *  - one reader thread per client: receives its messages and routes them;
 *  - watchdog thread: kills (SIGKILL) clients silent for longer than inactivityLimit
 *    and joins the threads of clients that have left.
 *
 * All routing happens under one mutex, so every participant sees messages in the same order.
 */
class Hub {
public:
    explicit Hub(HubListener& listener, HubOptions options = {});
    ~Hub();

    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    /// SIGUSR1 must already be blocked in the calling thread (and thus in all threads).
    void start();
    void stop();

    /// A message typed by the host's user.
    void sendFromHost(proto::ParticipantId to, std::string text);

    /// The host re-sends a message written by `author` to the common chat or to one participant.
    void forward(proto::ParticipantId author, std::string text, proto::ParticipantId to);

    [[nodiscard]] std::vector<ParticipantInfo> participants() const;
    [[nodiscard]] const std::string& hostName() const noexcept;

private:
    std::optional<proto::ParticipantId> acceptClient(pid_t pid);
    void readLoop(Participant& participant, const std::stop_token& stop);
    void handleHello(Participant& participant, const proto::Message& hello);
    void handleChat(Participant& participant, proto::Message message);
    void route(const proto::Message& message);
    void removeParticipant(proto::ParticipantId id, std::string reason);
    void watchdogLoop(const std::stop_token& stop);
    proto::ParticipantId freeIdLocked() const;

    HubListener& listener_;
    HubOptions options_;

    mutable std::mutex mutex_;
    std::map<proto::ParticipantId, std::unique_ptr<Participant>> participants_;
    std::vector<std::unique_ptr<Participant>>
        finished_; ///< left, waiting for their thread to be joined
    bool stopping_ = false;

    HandshakeListener handshake_;
    std::jthread watchdog_;
};

} // namespace chat::host
