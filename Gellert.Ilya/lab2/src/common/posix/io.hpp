#pragma once

#include <chrono>
#include <cstddef>
#include <span>

namespace chat::posix {

enum class Readiness {
    Readable, ///< there is data (possibly followed by end of stream)
    Closed,   ///< the other side closed it and nothing is left to read
    Timeout,
};

/// Waits until `fd` has data or is closed by the peer, at most `timeout` (poll).
[[nodiscard]] Readiness waitReadable(int fd, std::chrono::milliseconds timeout);

/// True if the peer has closed its end (POLLHUP/POLLRDHUP), even if unread data remains.
[[nodiscard]] bool peerClosed(int fd);

/// Writes all bytes, retrying after partial writes and EINTR. False if the peer is gone.
[[nodiscard]] bool writeAll(int fd, std::span<const std::byte> data);

/// Reads exactly `buffer.size()` bytes. False on end of stream or error.
[[nodiscard]] bool readExact(int fd, std::span<std::byte> buffer);

/// Switches O_NONBLOCK on or off.
void setNonBlocking(int fd, bool enabled);

/// Makes writes to a closed pipe/socket return EPIPE instead of killing the process.
void ignoreSigpipe();

} // namespace chat::posix
