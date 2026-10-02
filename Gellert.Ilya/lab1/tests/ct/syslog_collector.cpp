#include "ct/syslog_collector.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "common/test_utils.hpp"

namespace test {

SyslogCollector::SyslogCollector(std::filesystem::path socketPath)
    : socketPath_(std::move(socketPath))
{
    fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socketPath_.c_str());
    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string error = std::strerror(errno);
        close(fd_);
        throw std::runtime_error("cannot bind " + socketPath_.string() + ": " + error);
    }

    thread_ = std::thread([this] { receiveLoop(); });
}

SyslogCollector::~SyslogCollector()
{
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    close(fd_);
    unlink(socketPath_.c_str());
}

void SyslogCollector::receiveLoop()
{
    std::array<char, 8192> buffer{};
    while (!stop_) {
        pollfd pfd{fd_, POLLIN, 0};
        if (poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        const ssize_t len = recv(fd_, buffer.data(), buffer.size(), 0);
        if (len > 0) {
            const std::lock_guard lock(mutex_);
            messages_.emplace_back(buffer.data(), static_cast<std::size_t>(len));
        }
    }
}

std::vector<std::string> SyslogCollector::messages() const
{
    const std::lock_guard lock(mutex_);
    return messages_;
}

std::string SyslogCollector::find(const std::string& part) const
{
    const std::lock_guard lock(mutex_);
    const auto it = std::ranges::find_if(
        messages_, [&](const auto& message) { return contains(message, part); });
    return it == messages_.end() ? std::string() : *it;
}

std::size_t SyslogCollector::count(const std::string& part) const
{
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::ranges::count_if(
        messages_, [&](const auto& message) { return contains(message, part); }));
}

bool SyslogCollector::waitFor(const std::string& part, std::chrono::milliseconds timeout) const
{
    return waitUntil([&] { return count(part) > 0; }, timeout);
}

std::string SyslogCollector::dump() const
{
    std::string result;
    for (const auto& message : messages()) {
        result += message + '\n';
    }
    return result;
}

} // namespace test
