#pragma once

#include <sys/types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace chat::transport {

/// Identifies one host↔client channel; both sides derive file and semaphore names from it.
struct ConnId {
    pid_t hostPid = 0;
    std::uint32_t clientId = 0;
};

enum class ReadResult {
    Ok,      ///< a whole message was read
    Timeout, ///< nothing arrived in time; the channel is still usable
    Closed,  ///< the other side is gone (or broke the protocol); the channel is dead
};

/**
 * Two-way message channel between the host and one client.
 *
 * The same interface has three implementations, one per file: conn_fifo.cpp (mkfifo),
 * conn_sock.cpp (Unix sockets) and conn_pipe.cpp (pipe opened through /proc/<pid>/fd).
 * Every binary links exactly one of them, e.g. host_fifo = host.cpp + conn_fifo.cpp.
 *
 * Writes and reads are synchronized with two global semaphores (sem_open): the writer posts
 * after each message, the reader waits on it, at most 5 seconds.
 *
 * One thread may read while other threads write (callers serialize their writes).
 */
class Conn {
public:
    /// create = true: the host creates the channel; false: the client opens an existing one.
    /// Throws std::system_error if the channel cannot be created/opened.
    Conn(ConnId id, bool create);
    ~Conn();

    Conn(Conn&&) noexcept;
    Conn& operator=(Conn&&) noexcept;
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    /// Sends one message. False if the other side is gone.
    [[nodiscard]] bool write(std::span<const std::byte> message);

    /// Waits for one message, at most `timeout`.
    [[nodiscard]] ReadResult read(std::vector<std::byte>& message,
                                  std::chrono::milliseconds timeout);

    /// Short name of the implementation: "fifo", "sock" or "pipe".
    [[nodiscard]] static std::string_view typeName();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chat::transport
