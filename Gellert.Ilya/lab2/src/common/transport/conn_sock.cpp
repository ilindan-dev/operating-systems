// Conn over a Unix domain stream socket: the host listens on /tmp/chat-<pid>-<id>.sock.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

#include "posix/io.hpp"
#include "posix/unique_fd.hpp"
#include "transport/channel.hpp"
#include "transport/conn.hpp"

namespace chat::transport {

namespace {

sockaddr_un makeAddress(const std::string& path)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        channel::throwErrno("socket path " + path);
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return address;
}

posix::UniqueFd makeSocket()
{
    posix::UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) {
        channel::throwErrno("socket");
    }
    return fd;
}

} // namespace

struct Conn::Impl {
    Impl(ConnId id, bool create)
        : owner(create), path(channel::filePath(id, "sock")),
          up(channel::semaphore(id, "up", create)), down(channel::semaphore(id, "down", create))
    {
        const sockaddr_un address = makeAddress(path);
        const auto* raw = reinterpret_cast<const sockaddr*>(&address);
        if (owner) {
            listenFd = makeSocket();
            ::unlink(path.c_str());
            if (::bind(listenFd.get(), raw, sizeof(address)) != 0) {
                channel::throwErrno("bind " + path);
            }
            if (::listen(listenFd.get(), 1) != 0) {
                channel::throwErrno("listen " + path);
            }
        } else {
            fd = makeSocket();
            if (::connect(fd.get(), raw, sizeof(address)) != 0) {
                channel::throwErrno("connect " + path);
            }
        }
    }

    ~Impl()
    {
        if (owner) {
            ::unlink(path.c_str());
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    /// Host side: takes the client's connection if it has arrived within `timeout`.
    bool accept(std::chrono::milliseconds timeout)
    {
        if (posix::waitReadable(listenFd.get(), timeout) != posix::Readiness::Readable) {
            return false;
        }
        fd.reset(::accept4(listenFd.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (fd.valid()) {
            listenFd.reset(); // exactly one client per socket
        }
        return fd.valid();
    }

    bool owner;
    std::string path;
    posix::NamedSemaphore up;
    posix::NamedSemaphore down;
    posix::UniqueFd listenFd;
    posix::UniqueFd fd;
};

Conn::Conn(ConnId id, bool create) : impl_(std::make_unique<Impl>(id, create)) {}
Conn::~Conn() = default;
Conn::Conn(Conn&&) noexcept = default;
Conn& Conn::operator=(Conn&&) noexcept = default;

bool Conn::write(std::span<const std::byte> message)
{
    if (!impl_->fd.valid()) {
        return false; // host side before the client has connected
    }
    auto& ready = impl_->owner ? impl_->down : impl_->up;
    return channel::sendMessage(impl_->fd.get(), ready, message);
}

ReadResult Conn::read(std::vector<std::byte>& message, std::chrono::milliseconds timeout)
{
    if (!impl_->fd.valid() && !impl_->accept(timeout)) {
        return ReadResult::Timeout;
    }
    auto& ready = impl_->owner ? impl_->up : impl_->down;
    return channel::receiveMessage(impl_->fd.get(), ready, message, timeout);
}

std::string_view Conn::typeName()
{
    return "sock";
}

} // namespace chat::transport
