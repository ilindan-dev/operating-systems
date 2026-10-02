#pragma once

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace test {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

/**
 * @brief Unique temporary directory that is removed recursively in the destructor.
 */
class TempDir {
public:
    TempDir()
    {
        std::string pattern = (fs::temp_directory_path() / "disk-monitor-test-XXXXXX").string();
        if (mkdtemp(pattern.data()) == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = pattern;
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const
    {
        return path_;
    }

    fs::path operator/(const fs::path& relative) const
    {
        return path_ / relative;
    }

private:
    fs::path path_;
};

/**
 * @brief Changes the working directory and restores it in the destructor.
 */
class CurrentDirGuard {
public:
    explicit CurrentDirGuard(const fs::path& dir) : saved_(fs::current_path())
    {
        fs::current_path(dir);
    }

    ~CurrentDirGuard()
    {
        std::error_code ec;
        fs::current_path(saved_, ec);
    }

    CurrentDirGuard(const CurrentDirGuard&) = delete;
    CurrentDirGuard& operator=(const CurrentDirGuard&) = delete;

private:
    fs::path saved_;
};

/**
 * @brief A forked child process that is killed and reaped in the destructor.
 */
class ChildProcess {
public:
    /// Forks a child that runs `body` and then exits. The constructor returns only after
    /// `body` has signalled readiness by calling the function passed to it.
    explicit ChildProcess(const std::function<void(const std::function<void()>& ready)>& body)
    {
        std::array<int, 2> pipeFds{};
        if (pipe(pipeFds.data()) != 0) {
            throw std::runtime_error("pipe failed");
        }
        pid_ = fork();
        if (pid_ < 0) {
            throw std::runtime_error("fork failed");
        }
        if (pid_ == 0) {
            close(pipeFds[0]);
            const int writeFd = pipeFds[1];
            body([writeFd] {
                constexpr char byte = 1;
                [[maybe_unused]] auto written = write(writeFd, &byte, 1);
                close(writeFd);
            });
            _exit(0);
        }
        close(pipeFds[1]);
        char byte = 0;
        [[maybe_unused]] auto readBytes = read(pipeFds[0], &byte, 1);
        close(pipeFds[0]);
    }

    /// A child of the same program that just waits for signals (default dispositions).
    static ChildProcess sleeping()
    {
        return ChildProcess([](const std::function<void()>& ready) {
            ready();
            for (;;) {
                pause();
            }
        });
    }

    /// A child that has exec'ed another program (`sleep`), i.e. a "foreign" process.
    static ChildProcess foreign()
    {
        return ChildProcess([](const std::function<void()>& ready) {
            ready();
            execlp("sleep", "sleep", "60", static_cast<char*>(nullptr));
            _exit(127);
        });
    }

    ChildProcess(ChildProcess&& other) noexcept : pid_(other.pid_)
    {
        other.pid_ = -1;
    }

    ChildProcess& operator=(ChildProcess&&) = delete;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ~ChildProcess()
    {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }

    pid_t pid() const
    {
        return pid_;
    }

    /// Waits for the child to exit and returns the raw waitpid status.
    int wait()
    {
        int status = 0;
        waitpid(pid_, &status, 0);
        pid_ = -1;
        return status;
    }

private:
    pid_t pid_{-1};
};

/// PID of a process that has already exited and been reaped.
inline pid_t deadPid()
{
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(0);
    }
    waitpid(pid, nullptr, 0);
    return pid;
}

/// Writes `content` to the file, replacing it.
inline void writeFile(const fs::path& path, const std::string& content)
{
    std::ofstream file(path, std::ios::trunc);
    file << content;
    if (!file) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

/// Reads the whole file (empty string if it does not exist).
inline std::string readFile(const fs::path& path)
{
    const std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// Polls `condition` until it becomes true or the timeout expires.
inline bool waitUntil(const std::function<bool()>& condition,
                      std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (condition()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
}

/// Returns the message of the exception thrown by `action`, or an empty string.
template <typename Action> std::string errorMessage(Action&& action)
{
    try {
        action();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

/// True if `text` contains `part`.
inline bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace test
