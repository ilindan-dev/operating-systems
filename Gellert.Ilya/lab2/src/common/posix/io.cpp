#include "io.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <system_error>

namespace chat::posix {

Readiness waitReadable(int fd, std::chrono::milliseconds timeout)
{
    pollfd pfd{fd, POLLIN | POLLRDHUP, 0};
    int ready = 0;
    do {
        ready = poll(&pfd, 1, static_cast<int>(timeout.count()));
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) {
        throw std::system_error(errno, std::generic_category(), "poll");
    }
    if (ready == 0) {
        return Readiness::Timeout;
    }
    if ((pfd.revents & POLLIN) != 0) {
        return Readiness::Readable;
    }
    return Readiness::Closed; // POLLHUP / POLLERR / POLLNVAL without data
}

bool peerClosed(int fd)
{
    pollfd pfd{fd, POLLIN | POLLRDHUP, 0};
    if (poll(&pfd, 1, 0) <= 0) {
        return false;
    }
    return (pfd.revents & (POLLHUP | POLLRDHUP | POLLERR)) != 0;
}

bool writeAll(int fd, std::span<const std::byte> data)
{
    while (!data.empty()) {
        const ssize_t written = ::write(fd, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false; // EPIPE: the reader is gone
        }
        data = data.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

bool readExact(int fd, std::span<std::byte> buffer)
{
    while (!buffer.empty()) {
        const ssize_t got = ::read(fd, buffer.data(), buffer.size());
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (got == 0) {
            return false; // end of stream in the middle of a message
        }
        buffer = buffer.subspan(static_cast<std::size_t>(got));
    }
    return true;
}

void setNonBlocking(int fd, bool enabled)
{
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL)");
    }
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(fd, F_SETFL, updated) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL)");
    }
}

void ignoreSigpipe()
{
    std::signal(SIGPIPE, SIG_IGN);
}

} // namespace chat::posix
