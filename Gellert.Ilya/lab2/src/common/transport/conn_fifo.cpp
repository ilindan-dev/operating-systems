// Conn over two named pipes (mkfifo): "-up" carries client→host, "-down" host→client.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix/io.hpp"
#include "posix/unique_fd.hpp"
#include "transport/channel.hpp"
#include "transport/conn.hpp"

namespace chat::transport {

namespace {

constexpr mode_t kPermissions = 0600;

posix::UniqueFd openFifo(const std::string& path, int mode)
{
    // O_NONBLOCK so that open() never hangs waiting for the other side; reads and writes
    // are switched back to blocking right after.
    posix::UniqueFd fd(::open(path.c_str(), mode | O_NONBLOCK | O_CLOEXEC));
    if (!fd.valid()) {
        channel::throwErrno("open " + path);
    }
    posix::setNonBlocking(fd.get(), false);
    return fd;
}

} // namespace

struct Conn::Impl {
    Impl(const ConnId id, const bool create)
        : owner(create), upPath(channel::filePath(id, "up.fifo")),
          downPath(channel::filePath(id, "down.fifo")), up(channel::semaphore(id, "up", create)),
          down(channel::semaphore(id, "down", create))
    {
        if (owner) {
            for (const auto& path : {upPath, downPath}) {
                ::unlink(path.c_str());
                if (::mkfifo(path.c_str(), kPermissions) != 0) {
                    channel::throwErrno("mkfifo " + path);
                }
            }
            // A reader may open a FIFO before any writer exists. The writing end of "down"
            // can only be opened once the client is reading it, so that happens in read().
            readFd = openFifo(upPath, O_RDONLY);
        } else {
            readFd = openFifo(downPath, O_RDONLY);
            writeFd = openFifo(upPath, O_WRONLY); // the host already has it open for reading
        }
    }

    ~Impl()
    {
        if (owner) {
            ::unlink(upPath.c_str());
            ::unlink(downPath.c_str());
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    bool owner;
    std::string upPath;
    std::string downPath;
    posix::NamedSemaphore up;
    posix::NamedSemaphore down;
    posix::UniqueFd readFd;
    posix::UniqueFd writeFd;
};

Conn::Conn(ConnId id, bool create) : impl_(std::make_unique<Impl>(id, create)) {}
Conn::~Conn() = default;
Conn::Conn(Conn&&) noexcept = default;
Conn& Conn::operator=(Conn&&) noexcept = default;

bool Conn::write(const std::span<const std::byte> message)
{
    if (!impl_->writeFd.valid()) {
        return false; // host side before the client has said hello
    }
    auto& ready = impl_->owner ? impl_->down : impl_->up;
    return channel::sendMessage(impl_->writeFd.get(), ready, message);
}

ReadResult Conn::read(std::vector<std::byte>& message, const std::chrono::milliseconds timeout)
{
    auto& ready = impl_->owner ? impl_->up : impl_->down;
    const ReadResult result = channel::receiveMessage(impl_->readFd.get(), ready, message, timeout);

    if (result == ReadResult::Ok && impl_->owner && !impl_->writeFd.valid()) {
        // The client opens its reading end before it writes anything, so it is there now,
        // unless the client has already left; then the message is still delivered and the
        // next read() reports Closed.
        try {
            impl_->writeFd = openFifo(impl_->downPath, O_WRONLY);
        } catch (const std::system_error&) {
        }
    }
    return result;
}

std::string_view Conn::typeName()
{
    return "fifo";
}

} // namespace chat::transport
