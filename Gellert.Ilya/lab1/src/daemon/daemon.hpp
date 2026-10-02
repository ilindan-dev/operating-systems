#pragma once

#include <filesystem>

#include "config/config.hpp"
#include "worker/worker.hpp"

namespace monitor {

/// Default location of the PID file.
inline constexpr const char* kDefaultPidFile = "/tmp/disk_monitor.pid";

/**
 * @brief Core daemon class responsible for process daemonization and lifecycle management.
 *
 * Implements the Singleton pattern: a daemon exists in a single instance by its nature.
 *
 * Signals (SIGTERM, SIGINT, SIGHUP) are blocked and received through a signalfd, so they are
 * handled in the main loop together with inotify events instead of in an asynchronous handler.
 */
class Daemon {
public:
    /**
     * @brief Retrieves the single global instance of the Daemon.
     */
    static Daemon& getInstance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    /**
     * @brief Initializes the daemon with the provided configuration.
     * @param config      Parsed configuration (must contain the absolute config path).
     * @param pidFilePath Where to keep the PID file.
     */
    void init(const Config& config, const std::filesystem::path& pidFilePath = kDefaultPidFile);

    /**
     * @brief Starts the daemon.
     *
     * Stops a previous instance (via the PID file), daemonizes, and serves events until
     * SIGTERM/SIGINT. Returns only in the daemon process, after a clean shutdown.
     *
     * @throws std::exception On a fatal error (the caller logs it).
     */
    void run();

private:
    Daemon() = default;
    ~Daemon() = default;

    /// Blocks the handled signals and creates a signalfd for them.
    void setupSignals();

    /// Waits for signals and inotify events with poll() until a termination signal arrives.
    void eventLoop();

    /// Reads one signal from the signalfd and reacts to it.
    void handleSignal();

    /// Re-reads the config file (SIGHUP). On error the previous config is kept.
    void reloadConfig();

    /// Releases resources and removes the PID file.
    void shutdown();

    Config config_;
    std::filesystem::path pidFilePath_{kDefaultPidFile};
    Worker worker_;
    int signalFd_{-1};
    bool running_{false};
};

} // namespace monitor
