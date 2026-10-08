#include "named_semaphore.hpp"

#include <fcntl.h>

#include <cerrno>
#include <ctime>
#include <system_error>
#include <utility>

namespace chat::posix {

namespace {

constexpr mode_t kPermissions = 0600;

/// sem_timedwait takes an absolute CLOCK_REALTIME deadline, not a duration.
timespec deadlineAfter(const std::chrono::milliseconds timeout)
{
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    constexpr long kNanosPerSecond = 1'000'000'000;
    const long long totalNanos = now.tv_nsec + timeout.count() * 1'000'000LL;
    now.tv_sec += static_cast<time_t>(totalNanos / kNanosPerSecond);
    now.tv_nsec = static_cast<long>(totalNanos % kNanosPerSecond);
    return now;
}

} // namespace

NamedSemaphore NamedSemaphore::create(std::string name, const unsigned initial)
{
    sem_unlink(name.c_str()); // a leftover from a crashed run would keep its old value
    sem_t* sem = sem_open(name.c_str(), O_CREAT | O_EXCL, kPermissions, initial);
    if (sem == SEM_FAILED) {
        throw std::system_error(errno, std::generic_category(), "sem_open(create) " + name);
    }
    return NamedSemaphore(sem, std::move(name), true);
}

NamedSemaphore NamedSemaphore::open(std::string name)
{
    sem_t* sem = sem_open(name.c_str(), 0);
    if (sem == SEM_FAILED) {
        throw std::system_error(errno, std::generic_category(), "sem_open " + name);
    }
    return NamedSemaphore(sem, std::move(name), false);
}

NamedSemaphore::NamedSemaphore(sem_t* sem, std::string name, bool owner) noexcept
    : sem_(sem), name_(std::move(name)), owner_(owner)
{}

NamedSemaphore::~NamedSemaphore()
{
    close();
}

NamedSemaphore::NamedSemaphore(NamedSemaphore&& other) noexcept
    : sem_(std::exchange(other.sem_, SEM_FAILED)), name_(std::move(other.name_)),
      owner_(std::exchange(other.owner_, false))
{}

NamedSemaphore& NamedSemaphore::operator=(NamedSemaphore&& other) noexcept
{
    if (this != &other) {
        close();
        sem_ = std::exchange(other.sem_, SEM_FAILED);
        name_ = std::move(other.name_);
        owner_ = std::exchange(other.owner_, false);
    }
    return *this;
}

void NamedSemaphore::close() noexcept
{
    if (sem_ != SEM_FAILED) {
        sem_close(sem_);
        if (owner_) {
            sem_unlink(name_.c_str());
        }
        sem_ = SEM_FAILED;
    }
}

void NamedSemaphore::post()
{
    if (sem_post(sem_) != 0) {
        throw std::system_error(errno, std::generic_category(), "sem_post " + name_);
    }
}

bool NamedSemaphore::wait(const std::chrono::milliseconds timeout)
{
    const timespec deadline = deadlineAfter(timeout);
    while (sem_timedwait(sem_, &deadline) != 0) {
        if (errno == ETIMEDOUT) {
            return false;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "sem_timedwait " + name_);
        }
    }
    return true;
}

bool NamedSemaphore::tryWait()
{
    while (sem_trywait(sem_) != 0) {
        if (errno == EAGAIN) {
            return false;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "sem_trywait " + name_);
        }
    }
    return true;
}

const std::string& NamedSemaphore::name() const noexcept
{
    return name_;
}

} // namespace chat::posix
