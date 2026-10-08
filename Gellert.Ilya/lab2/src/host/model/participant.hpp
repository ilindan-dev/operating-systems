#pragma once

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "proto/message.hpp"
#include "transport/conn.hpp"

namespace chat::host {

/// One connected client as the host sees it.
struct Participant {
    Participant(proto::ParticipantId id, pid_t pid, transport::Conn conn);

    Participant(const Participant&) = delete;
    Participant& operator=(const Participant&) = delete;

    /// Sends a message to this client. Safe to call from several threads.
    bool send(const proto::Message& message);

    const proto::ParticipantId id;
    const pid_t pid;
    std::string name;    ///< set by Hello; guarded by Hub's mutex
    bool joined = false; ///< Hello received; guarded by Hub's mutex
    std::atomic<std::int64_t> lastActivityMs{0};
    std::atomic<bool> kicked{false};

    transport::Conn conn;
    std::mutex writeMutex; ///< one message at a time through `conn`

    /// Reads this client's messages. Declared last: it is joined before `conn` is destroyed.
    std::jthread reader;
};

} // namespace chat::host
