#pragma once

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace test {

/// Polls `predicate` until it is true or `timeout` passes.
inline bool waitUntil(const std::function<bool()>& predicate,
                      std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

/// Records events from other threads; tests wait for them with a timeout.
class EventLog {
public:
    template <typename F> void update(F&& change)
    {
        {
            const std::lock_guard lock(mutex_);
            change();
        }
        changed_.notify_all();
    }

    template <typename F>
    bool waitFor(F&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, predicate);
    }

    template <typename F> auto read(F&& reader)
    {
        const std::lock_guard lock(mutex_);
        return reader();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
};

/**
 * A forked process that runs `body` and exits with its return value.
 * It waits for go() before starting, so the parent can fork first (while it has no threads)
 * and start its own threads afterwards.
 */
class ChildProcess {
public:
    explicit ChildProcess(const std::function<int()>& body)
    {
        std::array<int, 2> fds{};
        if (pipe(fds.data()) != 0) {
            return;
        }
        pid_ = fork();
        if (pid_ == 0) {
            close(fds[1]);
            char go = 0;
            const bool started = read(fds[0], &go, 1) == 1;
            close(fds[0]);
            _exit(started ? body() : 100);
        }
        close(fds[0]);
        goFd_ = fds[1];
    }

    ~ChildProcess()
    {
        if (goFd_ >= 0) {
            close(goFd_);
        }
        if (pid_ > 0 && !reaped_) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    void go()
    {
        const char byte = 1;
        [[maybe_unused]] const auto written = write(goFd_, &byte, 1);
        close(goFd_);
        goFd_ = -1;
    }

    /// Waits for the child to exit; returns the raw status from waitpid, or -1 on timeout.
    int wait(std::chrono::milliseconds timeout = std::chrono::seconds(15))
    {
        int status = -1;
        const bool exited =
            waitUntil([&] { return waitpid(pid_, &status, WNOHANG) == pid_; }, timeout);
        reaped_ = exited;
        return exited ? status : -1;
    }

    /// Exit code of a child that exited normally, -1 otherwise.
    int exitCode(std::chrono::milliseconds timeout = std::chrono::seconds(15))
    {
        const int status = wait(timeout);
        return status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    [[nodiscard]] pid_t pid() const
    {
        return pid_;
    }

private:
    pid_t pid_ = -1;
    int goFd_ = -1;
    bool reaped_ = false;
};

} // namespace test
