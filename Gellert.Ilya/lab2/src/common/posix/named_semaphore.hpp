#pragma once

#include <semaphore.h>

#include <chrono>
#include <string>

namespace chat::posix {

/**
 * RAII wrapper for a POSIX named semaphore (sem_open).
 *
 * A named semaphore lives in the kernel (/dev/shm/sem.<name>) and is visible to unrelated
 * processes by name. The creator owns the name and removes it (sem_unlink) on destruction;
 * other processes only open it.
 */
class NamedSemaphore {
public:
    /// Creates a new semaphore with `initial` value, replacing a stale one with the same name.
    static NamedSemaphore create(std::string name, unsigned initial = 0);
    /// Opens a semaphore created by another process. Throws std::system_error if it is missing.
    static NamedSemaphore open(std::string name);

    ~NamedSemaphore();
    NamedSemaphore(NamedSemaphore&& other) noexcept;
    NamedSemaphore& operator=(NamedSemaphore&& other) noexcept;
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    /// Increments the value and wakes one waiter.
    void post();
    /// Decrements the value, waiting at most `timeout`. Returns false on timeout.
    [[nodiscard]] bool wait(std::chrono::milliseconds timeout);
    /// Decrements the value only if it is positive right now.
    [[nodiscard]] bool tryWait();

    [[nodiscard]] const std::string& name() const noexcept;

private:
    NamedSemaphore(sem_t* sem, std::string name, bool owner) noexcept;
    void close() noexcept;

    sem_t* sem_ = SEM_FAILED;
    std::string name_;
    bool owner_ = false;
};

} // namespace chat::posix
