#include "pid_manager.hpp"

#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace monitor {

namespace {

fs::path procPath(pid_t pid)
{
    return fs::path("/proc") / std::to_string(pid);
}

} // namespace

std::optional<pid_t> PidManager::readPidFile(const fs::path& pidFilePath)
{
    std::ifstream pidFile(pidFilePath);
    if (!pidFile.is_open()) {
        return std::nullopt;
    }
    long pid = 0;
    if (!(pidFile >> pid) || pid <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(pid);
}

bool PidManager::isProcessAlive(pid_t pid)
{
    // /proc/<pid>/stat looks like "1234 (name) S ...". The name may contain ')' itself,
    // so the state is the first character after the *last* ')'.
    std::ifstream stat(procPath(pid) / "stat");
    if (!stat.is_open()) {
        return false;
    }
    std::string content;
    std::getline(stat, content);
    const auto pos = content.rfind(')');
    if (pos == std::string::npos || pos + 2 >= content.size()) {
        return false;
    }
    const char state = content[pos + 2];
    return state != 'Z' && state != 'X';
}

std::string PidManager::processName(pid_t pid)
{
    std::ifstream comm(procPath(pid) / "comm");
    std::string name;
    std::getline(comm, name);
    return name;
}

bool PidManager::terminateRunningInstance(const fs::path& pidFilePath,
                                          std::chrono::milliseconds timeout)
{
    const auto oldPid = readPidFile(pidFilePath);
    if (!oldPid || *oldPid == getpid()) {
        return false;
    }

    if (!isProcessAlive(*oldPid)) {
        syslog(LOG_INFO, "Stale PID file %s: process %d is not running", pidFilePath.c_str(),
               *oldPid);
        return false;
    }

    // The PID may have been reused by an unrelated program after a crash.
    const std::string oldName = processName(*oldPid);
    const std::string ownName = processName(getpid());
    if (oldName != ownName) {
        syslog(LOG_WARNING, "PID %d from %s belongs to '%s', not to '%s'; leaving it alone",
               *oldPid, pidFilePath.c_str(), oldName.c_str(), ownName.c_str());
        return false;
    }

    if (kill(*oldPid, SIGTERM) != 0) {
        syslog(LOG_ERR, "Failed to send SIGTERM to previous instance (PID %d): %s", *oldPid,
               std::strerror(errno));
        return false;
    }
    syslog(LOG_INFO, "Sent SIGTERM to previous instance (PID %d)", *oldPid);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (isProcessAlive(*oldPid)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            syslog(LOG_WARNING, "Previous instance (PID %d) did not exit within %lld ms", *oldPid,
                   static_cast<long long>(timeout.count()));
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

void PidManager::writePidFile(const fs::path& pidFilePath)
{
    std::ofstream pidFile(pidFilePath, std::ios::trunc);
    if (!pidFile.is_open()) {
        throw std::system_error(errno, std::generic_category(),
                                "Cannot open PID file " + pidFilePath.string());
    }
    pidFile << getpid() << '\n';
    pidFile.close();
    if (!pidFile) {
        throw std::system_error(errno, std::generic_category(),
                                "Cannot write PID file " + pidFilePath.string());
    }
}

void PidManager::removePidFileIfOwned(const fs::path& pidFilePath)
{
    // The new instance may already have written its own PID here; do not delete it.
    if (readPidFile(pidFilePath) == getpid()) {
        std::error_code ec;
        fs::remove(pidFilePath, ec);
    }
}

} // namespace monitor
