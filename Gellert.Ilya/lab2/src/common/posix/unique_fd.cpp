#include "unique_fd.hpp"

#include <unistd.h>

#include <memory>
#include <utility>

namespace chat::posix {

UniqueFd::UniqueFd(const int fd) noexcept : fd_{fd} {}

UniqueFd::~UniqueFd()
{
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != std::addressof(other)) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::get() const noexcept
{
    return fd_;
}

bool UniqueFd::valid() const noexcept
{
    return fd_ >= 0;
}

int UniqueFd::release() noexcept
{
    return std::exchange(fd_, -1);
}

void UniqueFd::reset(const int fd) noexcept
{
    if (fd_ != fd) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }
}

} // namespace chat::posix
