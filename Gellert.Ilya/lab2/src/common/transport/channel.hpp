#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "posix/named_semaphore.hpp"
#include "transport/conn.hpp"

/// Pieces shared by all Conn implementations.
namespace chat::transport::channel {

inline constexpr std::size_t kMaxMessageSize = 64 * 1024;

/// "/tmp/chat-<host pid>-<client id>-<suffix>"
[[nodiscard]] std::string filePath(ConnId id, std::string_view suffix);

/// The two semaphores of a channel: "up" counts client→host messages, "down" host→client.
[[nodiscard]] posix::NamedSemaphore semaphore(ConnId id, std::string_view direction, bool create);

/// Writes [size][bytes] to `fd`, then posts `ready`. False if the reader is gone.
[[nodiscard]] bool sendMessage(int fd, posix::NamedSemaphore& ready,
                               std::span<const std::byte> message);

/// Waits for data on `fd`, takes one token from `ready` (≤ 5 s) and reads one message.
[[nodiscard]] ReadResult receiveMessage(int fd, posix::NamedSemaphore& ready,
                                        std::vector<std::byte>& message,
                                        std::chrono::milliseconds timeout);

/// Throws std::system_error built from errno.
[[noreturn]] void throwErrno(const std::string& what);

} // namespace chat::transport::channel
