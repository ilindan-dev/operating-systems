#include "ct/daemon_runner.hpp"

#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "common/test_utils.hpp"
#include "daemon/pid_manager.hpp"

namespace test {

LaunchResult launchDaemon(const std::vector<std::string>& args, const fs::path& cwd)
{
    // Everything that allocates memory is prepared before fork(): the test process has
    // other threads (SyslogCollector), and after fork() only async-signal-safe calls are safe.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(DISK_MONITOR_BIN));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    std::array<int, 2> pipeFds{};
    if (pipe(pipeFds.data()) != 0) {
        throw std::runtime_error("pipe failed");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        if (chdir(cwd.c_str()) != 0) {
            _exit(126);
        }
        execv(DISK_MONITOR_BIN, argv.data());
        _exit(127);
    }

    // Parent: read until EOF. The daemon reopens 0-2 on /dev/null, so it does not keep the
    // pipe open; EOF comes when the launcher and the intermediate process have exited.
    close(pipeFds[1]);
    LaunchResult result;
    std::array<char, 1024> buffer{};
    ssize_t len = 0;
    while ((len = read(pipeFds[0], buffer.data(), buffer.size())) > 0) {
        result.output.append(buffer.data(), static_cast<std::size_t>(len));
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

std::optional<ProcStat> readProcStat(pid_t pid)
{
    const std::string content = readFile(fs::path("/proc") / std::to_string(pid) / "stat");
    const auto pos = content.rfind(')');
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    // After "pid (comm) ": state ppid pgrp session tty_nr ...
    std::istringstream fields(content.substr(pos + 2));
    ProcStat stat;
    if (!(fields >> stat.state >> stat.ppid >> stat.pgrp >> stat.session >> stat.ttyNr)) {
        return std::nullopt;
    }
    return stat;
}

std::string readProcStatus(pid_t pid, const std::string& key)
{
    std::ifstream status(fs::path("/proc") / std::to_string(pid) / "status");
    std::string line;
    const std::string prefix = key + ":";
    while (std::getline(status, line)) {
        if (line.starts_with(prefix)) {
            const auto start = line.find_first_not_of(" \t", prefix.size());
            return start == std::string::npos ? "" : line.substr(start);
        }
    }
    return {};
}

std::string readLink(const fs::path& link)
{
    std::error_code ec;
    const fs::path target = fs::read_symlink(link, ec);
    return ec ? std::string() : target.string();
}

void becomeSubreaper()
{
    prctl(PR_SET_CHILD_SUBREAPER, 1);
}

void reapZombies()
{
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

std::vector<pid_t> findDaemons()
{
    std::vector<pid_t> result;
    for (const auto& entry : fs::directory_iterator("/proc")) {
        const std::string name = entry.path().filename().string();
        if (name.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        const auto pid = static_cast<pid_t>(std::stol(name));
        if (monitor::PidManager::processName(pid) == "disk_monitor" &&
            monitor::PidManager::isProcessAlive(pid)) {
            result.push_back(pid);
        }
    }
    return result;
}

void killAllDaemons()
{
    for (const pid_t pid : findDaemons()) {
        kill(pid, SIGKILL);
    }
    waitUntil([] {
        reapZombies();
        return findDaemons().empty();
    });
}

} // namespace test
