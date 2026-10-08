#include "channel.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <format>
#include <system_error>

#include "posix/io.hpp"
#include "posix/log.hpp"
#include "proto/constants.hpp"

namespace chat::transport::channel {

std::string filePath(ConnId id, std::string_view suffix)
{
    return std::format("/tmp/chat-{}-{}-{}", id.hostPid, id.clientId, suffix);
}

posix::NamedSemaphore semaphore(ConnId id, std::string_view direction, bool create)
{
    std::string name = std::format("/chat-{}-{}-{}", id.hostPid, id.clientId, direction);
    return create ? posix::NamedSemaphore::create(std::move(name))
                  : posix::NamedSemaphore::open(std::move(name));
}

bool sendMessage(const int fd, posix::NamedSemaphore& ready,
                 const std::span<const std::byte> message)
{
    if (message.size() > kMaxMessageSize) {
        return false;
    }
    const auto size = static_cast<std::uint32_t>(message.size());
    std::vector<std::byte> frame(sizeof(size) + message.size());
    std::memcpy(frame.data(), &size, sizeof(size));
    std::memcpy(frame.data() + sizeof(size), message.data(), message.size());

    if (!posix::writeAll(fd, frame)) {
        return false;
    }
    ready.post(); // one token per message: the reader may now take exactly one
    return true;
}

ReadResult receiveMessage(const int fd, posix::NamedSemaphore& ready,
                          std::vector<std::byte>& message, const std::chrono::milliseconds timeout)
{
    switch (posix::waitReadable(fd, timeout)) {
    case posix::Readiness::Timeout:
        return ReadResult::Timeout;
    case posix::Readiness::Closed:
        return ReadResult::Closed;
    case posix::Readiness::Readable:
        break;
    }

    // Data is there; the writer posts the token right after writing it.
    // If the peer has already closed, only a message it managed to post is still worth reading.
    const bool gotToken = posix::peerClosed(fd) ? ready.tryWait() : ready.wait(proto::kSyncTimeout);
    if (!gotToken) {
        if (!posix::peerClosed(fd)) {
            log::warning("no semaphore token within 5 s, closing the channel");
        }
        return ReadResult::Closed;
    }

    std::uint32_t size = 0;
    if (!posix::readExact(fd, std::as_writable_bytes(std::span(&size, 1))) ||
        size > kMaxMessageSize) {
        return ReadResult::Closed;
    }
    message.resize(size);
    return posix::readExact(fd, message) ? ReadResult::Ok : ReadResult::Closed;
}

void throwErrno(const std::string& what)
{
    throw std::system_error(errno, std::generic_category(), what);
}

} // namespace chat::transport::channel
