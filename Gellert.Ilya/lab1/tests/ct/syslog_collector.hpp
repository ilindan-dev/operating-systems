#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace test {

/**
 * @brief Fake syslog daemon: listens on /dev/log and stores every received message.
 *
 * syslog() sends datagrams like "<134>Oct  2 10:00:00 disk-monitor[42]: text" to the
 * /dev/log Unix socket. Binding our own socket there lets the tests read what the daemon logs.
 * This replaces the system logger, which is one of the reasons the component tests run only
 * inside a container.
 */
class SyslogCollector {
public:
    explicit SyslogCollector(std::filesystem::path socketPath = "/dev/log");
    ~SyslogCollector();

    SyslogCollector(const SyslogCollector&) = delete;
    SyslogCollector& operator=(const SyslogCollector&) = delete;

    /// All raw messages received so far.
    std::vector<std::string> messages() const;

    /// The first message containing `part`, or an empty string.
    std::string find(const std::string& part) const;

    /// Number of messages containing `part`.
    std::size_t count(const std::string& part) const;

    /// Waits until a message containing `part` arrives.
    bool waitFor(const std::string& part,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3)) const;

    /// All messages joined with newlines (for assertion output).
    std::string dump() const;

private:
    void receiveLoop();

    std::filesystem::path socketPath_;
    int fd_{-1};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
    std::thread thread_;
};

} // namespace test
