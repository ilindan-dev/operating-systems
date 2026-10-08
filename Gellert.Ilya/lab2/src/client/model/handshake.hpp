#pragma once

#include <sys/types.h>

#include <cstdint>

namespace chat::client {

/**
 * Client side of the handshake: sends SIGUSR1 to the host and waits (at most 5 s) for the
 * host's SIGUSR1 carrying this client's id.
 *
 * Ordinary signals from several clients at the same moment merge into one, so a knock can be
 * lost; then the wait times out and the client knocks again, up to `attempts` times.
 *
 * SIGUSR1 must be blocked in the calling thread. Throws std::runtime_error with a message
 * for the user if the host is missing, refuses or does not answer.
 */
[[nodiscard]] std::uint32_t knock(pid_t hostPid, int attempts = 3);

} // namespace chat::client
