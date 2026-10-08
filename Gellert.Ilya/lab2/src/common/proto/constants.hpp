#pragma once

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>

namespace chat::proto {

using ParticipantId = std::uint32_t;

/// The host is participant 0; clients get 1, 2, 3...
inline constexpr ParticipantId kHostId = 0;
/// `to` value of a message for everyone.
inline constexpr ParticipantId kBroadcast = 0xFFFF'FFFF;
/// "No participant", e.g. Message::forwardedFrom of a message that was not forwarded.
inline constexpr ParticipantId kNobody = 0xFFFF'FFFE;

/// Signal used for the handshake in both directions.
inline constexpr int kHandshakeSignal = SIGUSR1;

/// Every wait on a semaphore or a signal (except waiting for the user) gives up after this.
inline constexpr std::chrono::seconds kSyncTimeout{5};
/// A client that sends nothing for this long is killed with SIGKILL.
inline constexpr std::chrono::seconds kInactivityLimit{60};

inline constexpr std::size_t kMaxTextSize = 4096;
inline constexpr std::size_t kMaxNameSize = 32;

} // namespace chat::proto
