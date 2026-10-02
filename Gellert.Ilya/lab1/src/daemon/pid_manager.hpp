#pragma once

#include <sys/types.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace monitor {

/**
 * @brief Manages PID file operations for the daemon.
 */
class PidManager {
public:
    /**
     * @brief Reads a PID from the file.
     * @return The PID, or std::nullopt if the file is missing or does not contain a positive
     * number.
     */
    static std::optional<pid_t> readPidFile(const std::filesystem::path& pidFilePath);

    /**
     * @brief Checks via /proc whether a process with this PID exists and is not a zombie.
     */
    static bool isProcessAlive(pid_t pid);

    /**
     * @brief Returns the process name from /proc/<pid>/comm, or an empty string if unavailable.
     */
    static std::string processName(pid_t pid);

    /**
     * @brief Stops a previously started instance of this program.
     *
     * Reads the PID file, checks the process via /proc, makes sure it is the same program
     * (protection against PID reuse), sends SIGTERM and waits until the process exits.
     *
     * @param pidFilePath Path to the PID file.
     * @param timeout     How long to wait for the old process to exit.
     * @return true if an old instance was found and has exited.
     */
    static bool
    terminateRunningInstance(const std::filesystem::path& pidFilePath,
                             std::chrono::milliseconds timeout = std::chrono::seconds(5));

    /**
     * @brief Writes the current process's PID to the specified PID file.
     * @throws std::system_error If the file cannot be written.
     */
    static void writePidFile(const std::filesystem::path& pidFilePath);

    /**
     * @brief Removes the PID file, but only if it still contains the current process's PID.
     */
    static void removePidFileIfOwned(const std::filesystem::path& pidFilePath);
};

} // namespace monitor
