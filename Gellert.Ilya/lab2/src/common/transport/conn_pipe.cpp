// Conn over two anonymous pipes (pipe2) created by the host.
//
// An unrelated process cannot inherit them, but on Linux it can open them by path:
// /proc/<host pid>/fd/<number>. The host moves the pipe ends to numbers computed from the
// client id, so the client knows the paths without any extra exchange.
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <array>
#include <format>

#include "posix/io.hpp"
#include "posix/unique_fd.hpp"
#include "transport/channel.hpp"
#include "transport/conn.hpp"

namespace chat::transport {

namespace {

constexpr int kFdBase = 400;
constexpr int kFdsPerClient = 4;

/// Fixed descriptor numbers in the host process for client `clientId`.
struct PipeFds {
    int upRead;    ///< host reads client→host
    int upWrite;   ///< opened by the client through /proc
    int downRead;  ///< opened by the client through /proc
    int downWrite; ///< host writes host→client
};

PipeFds fdsFor(const std::uint32_t clientId)
{
    const int base = kFdBase + static_cast<int>(clientId) * kFdsPerClient;
    return {base, base + 1, base + 2, base + 3};
}

void ensureFdLimit(const int highestFd)
{
    rlimit limit{};
    getrlimit(RLIMIT_NOFILE, &limit);
    if (limit.rlim_cur > static_cast<rlim_t>(highestFd)) {
        return;
    }
    limit.rlim_cur = std::min<rlim_t>(limit.rlim_max, static_cast<rlim_t>(highestFd) + 64);
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur <= static_cast<rlim_t>(highestFd)) {
        errno = EMFILE;
        channel::throwErrno("too many clients for pipe descriptors");
    }
}

/// Moves `fd` to exactly number `target` (which must be free).
posix::UniqueFd moveTo(posix::UniqueFd fd, const int target)
{
    if (fcntl(target, F_GETFD) != -1) {
        errno = EBUSY;
        channel::throwErrno(std::format("descriptor {} is already in use", target));
    }
    if (::dup3(fd.get(), target, O_CLOEXEC) != target) {
        channel::throwErrno("dup3");
    }
    return posix::UniqueFd(target); // the original number is closed by `fd`
}

posix::UniqueFd openFromHost(pid_t hostPid, int number, const int mode)
{
    const std::string path = std::format("/proc/{}/fd/{}", hostPid, number);
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
        : owner(create), up(channel::semaphore(id, "up", create)),
          down(channel::semaphore(id, "down", create))
    {
        const PipeFds fds = fdsFor(id.clientId);
        if (owner) {
            ensureFdLimit(fds.downWrite);
            std::array<int, 2> upPipe{};
            std::array<int, 2> downPipe{};
            if (::pipe2(upPipe.data(), O_CLOEXEC) != 0) {
                channel::throwErrno("pipe2");
            }
            posix::UniqueFd upReadTmp(upPipe[0]);
            posix::UniqueFd upWriteTmp(upPipe[1]);
            if (::pipe2(downPipe.data(), O_CLOEXEC) != 0) {
                channel::throwErrno("pipe2");
            }
            posix::UniqueFd downReadTmp(downPipe[0]);
            posix::UniqueFd downWriteTmp(downPipe[1]);

            readFd = moveTo(std::move(upReadTmp), fds.upRead);
            clientUpWrite = moveTo(std::move(upWriteTmp), fds.upWrite);
            clientDownRead = moveTo(std::move(downReadTmp), fds.downRead);
            writeFd = moveTo(std::move(downWriteTmp), fds.downWrite);
        } else {
            readFd = openFromHost(id.hostPid, fds.downRead, O_RDONLY);
            writeFd = openFromHost(id.hostPid, fds.upWrite, O_WRONLY);
        }
    }

    bool owner;
    posix::NamedSemaphore up;
    posix::NamedSemaphore down;
    posix::UniqueFd readFd;
    posix::UniqueFd writeFd;
    // Host side: the client's ends stay open in the host until the client has opened its
    // own copies through /proc; then they are closed, otherwise the host would never see EOF.
    posix::UniqueFd clientUpWrite;
    posix::UniqueFd clientDownRead;
};

Conn::Conn(ConnId id, bool create) : impl_(std::make_unique<Impl>(id, create)) {}
Conn::~Conn() = default;
Conn::Conn(Conn&&) noexcept = default;
Conn& Conn::operator=(Conn&&) noexcept = default;

bool Conn::write(const std::span<const std::byte> message)
{
    if (impl_->owner && impl_->clientDownRead.valid()) {
        return false; // host side before the client has said hello
    }
    auto& ready = impl_->owner ? impl_->down : impl_->up;
    return channel::sendMessage(impl_->writeFd.get(), ready, message);
}

ReadResult Conn::read(std::vector<std::byte>& message, const std::chrono::milliseconds timeout)
{
    auto& ready = impl_->owner ? impl_->up : impl_->down;
    const ReadResult result = channel::receiveMessage(impl_->readFd.get(), ready, message, timeout);
    if (result == ReadResult::Ok && impl_->owner) {
        // The client wrote, so it has opened both ends: drop the host's spare copies.
        impl_->clientUpWrite.reset();
        impl_->clientDownRead.reset();
    }
    return result;
}

std::string_view Conn::typeName()
{
    return "pipe";
}

} // namespace chat::transport
