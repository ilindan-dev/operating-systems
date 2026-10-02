#pragma once

#include <sys/inotify.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace monitor {

/**
 * @brief A single file system event reported by inotify.
 */
struct FsEvent {
    std::filesystem::path path; ///< Full path of the file or directory the event refers to.
    std::uint32_t mask = 0;     ///< Raw inotify event mask (IN_OPEN, IN_MODIFY, ...).
    bool isDir = false;         ///< True if the subject of the event is a directory.
};

/**
 * @brief Converts an inotify mask into a readable string, e.g. "CLOSE_WRITE|ISDIR".
 */
std::string describeMask(std::uint32_t mask);

/**
 * @brief Formats an event as a log line, e.g. "OPEN file /home/user/a.txt".
 */
std::string formatEvent(const FsEvent& event);

/**
 * @brief Watches directories (recursively) with inotify and reports every event.
 *
 * The worker does not block and does not run its own loop: the owner polls fd() and calls
 * processEvents() when it becomes readable. This lets the daemon wait for signals and
 * file system events in a single poll() call.
 */
class Worker {
public:
    /// Callback that receives every event. By default events are written to syslog.
    using EventSink = std::function<void(const FsEvent&)>;

    /// Events inotify is asked to report.
    static constexpr std::uint32_t kWatchMask = IN_ALL_EVENTS | IN_ONLYDIR;

    Worker();
    explicit Worker(EventSink sink);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    /**
     * @brief Creates the inotify instance and starts watching the directories.
     * @throws std::system_error If inotify cannot be initialized.
     */
    void start(const std::vector<std::filesystem::path>& directories);

    /**
     * @brief Replaces the set of watched directories (used on SIGHUP).
     */
    void reload(const std::vector<std::filesystem::path>& directories);

    /**
     * @brief Removes all watches and closes the inotify descriptor.
     */
    void stop();

    /// inotify file descriptor to poll, or -1 if the worker is not started.
    int fd() const;

    /// Number of directories currently watched (including subdirectories).
    std::size_t watchCount() const;

    /**
     * @brief Reads all pending events without blocking and passes them to the sink.
     * @return Number of events processed.
     */
    std::size_t processEvents();

private:
    void watchRoots(const std::vector<std::filesystem::path>& directories);
    bool addWatchRecursive(const std::filesystem::path& dir);
    bool addWatch(const std::filesystem::path& dir);
    void removeWatchesUnder(const std::filesystem::path& dir);
    void removeAllWatches();
    void handleEvent(const inotify_event& event);

    EventSink sink_;
    int inotifyFd_{-1};

    /// Watch descriptor -> watched directory.
    std::unordered_map<int, std::filesystem::path> watches_;
};

} // namespace monitor
