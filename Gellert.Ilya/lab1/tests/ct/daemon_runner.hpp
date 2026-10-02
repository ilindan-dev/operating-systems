#pragma once

#include <sys/types.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace test {

/// Result of running the disk_monitor launcher (the process started from the "terminal").
struct LaunchResult {
    int exitCode = -1;  ///< Exit code of the launcher process.
    std::string output; ///< What the launcher printed to stdout and stderr.
};

/**
 * @brief Runs the disk_monitor binary with the arguments in `cwd` and waits for the launcher
 * to exit. The daemon itself keeps running in the background.
 */
LaunchResult launchDaemon(const std::vector<std::string>& args, const std::filesystem::path& cwd);

/// Fields of /proc/<pid>/stat that describe how a process is attached to the system.
struct ProcStat {
    char state = '?';
    pid_t ppid = 0;
    pid_t pgrp = 0;
    pid_t session = 0;
    int ttyNr = 0; ///< Controlling terminal, 0 if none.
};

std::optional<ProcStat> readProcStat(pid_t pid);

/// Value of a "Key:" line in /proc/<pid>/status, e.g. "Umask" -> "0027".
std::string readProcStatus(pid_t pid, const std::string& key);

/// Target of a symlink such as /proc/<pid>/cwd, or an empty string.
std::string readLink(const std::filesystem::path& link);

/// Makes the test process adopt orphaned descendants, so the daemons become its children
/// after the double fork and can be reaped instead of hanging around as zombies.
void becomeSubreaper();

/// Reaps all exited children without blocking.
void reapZombies();

/// PIDs of all running disk_monitor processes.
std::vector<pid_t> findDaemons();

/// Kills every disk_monitor process (SIGKILL) and waits until they are gone.
void killAllDaemons();

} // namespace test
