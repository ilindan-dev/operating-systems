#include "worker.hpp"

#include <syslog.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <ranges>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace monitor {

namespace {

struct MaskName {
    std::uint32_t bit;
    const char* name;
};

constexpr std::array<MaskName, 16> kMaskNames = {{
    {IN_ACCESS, "ACCESS"},
    {IN_MODIFY, "MODIFY"},
    {IN_ATTRIB, "ATTRIB"},
    {IN_CLOSE_WRITE, "CLOSE_WRITE"},
    {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
    {IN_OPEN, "OPEN"},
    {IN_MOVED_FROM, "MOVED_FROM"},
    {IN_MOVED_TO, "MOVED_TO"},
    {IN_CREATE, "CREATE"},
    {IN_DELETE, "DELETE"},
    {IN_DELETE_SELF, "DELETE_SELF"},
    {IN_MOVE_SELF, "MOVE_SELF"},
    {IN_UNMOUNT, "UNMOUNT"},
    {IN_Q_OVERFLOW, "Q_OVERFLOW"},
    {IN_IGNORED, "IGNORED"},
    {IN_ISDIR, "ISDIR"},
}};

void logEvent(const FsEvent& event)
{
    syslog(LOG_INFO, "%s", formatEvent(event).c_str());
}

/// True if `path` is `base` itself or lies inside it.
bool isWithin(const fs::path& path, const fs::path& base)
{
    return std::ranges::mismatch(base, path).in1 == base.end();
}

} // namespace

std::string describeMask(std::uint32_t mask)
{
    std::string result;
    for (const auto& [bit, name] : kMaskNames) {
        if ((mask & bit) != 0) {
            if (!result.empty()) {
                result += '|';
            }
            result += name;
            mask &= ~bit;
        }
    }
    if (mask != 0) {
        if (!result.empty()) {
            result += '|';
        }
        result += std::format("{:#x}", mask);
    }
    return result.empty() ? "NONE" : result;
}

std::string formatEvent(const FsEvent& event)
{
    return describeMask(event.mask & ~IN_ISDIR) + (event.isDir ? " dir " : " file ") +
           event.path.string();
}

Worker::Worker() : Worker(logEvent) {}

Worker::Worker(EventSink sink) : sink_(std::move(sink)) {}

Worker::~Worker()
{
    stop();
}

void Worker::start(const std::vector<fs::path>& directories)
{
    stop();
    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "inotify_init1 failed");
    }
    watchRoots(directories);
}

void Worker::watchRoots(const std::vector<fs::path>& directories)
{
    for (const auto& dir : directories) {
        if (addWatchRecursive(dir)) {
            syslog(LOG_INFO, "Monitoring directory: %s", dir.c_str());
        }
    }
}

void Worker::reload(const std::vector<fs::path>& directories)
{
    removeAllWatches();
    watchRoots(directories);
}

void Worker::stop()
{
    if (inotifyFd_ >= 0) {
        removeAllWatches();
        close(inotifyFd_);
        inotifyFd_ = -1;
    }
    watches_.clear();
}

int Worker::fd() const
{
    return inotifyFd_;
}

std::size_t Worker::watchCount() const
{
    return watches_.size();
}

bool Worker::addWatch(const fs::path& dir)
{
    const int wd = inotify_add_watch(inotifyFd_, dir.c_str(), kWatchMask);
    if (wd < 0) {
        syslog(LOG_ERR, "Failed to watch directory %s: %s", dir.c_str(), std::strerror(errno));
        return false;
    }
    watches_[wd] = dir;
    return true;
}

bool Worker::addWatchRecursive(const fs::path& dir)
{
    // First collect the subdirectories, then add the watches. In the opposite order the
    // daemon's own reading of the directories would be reported as OPEN/ACCESS events.
    std::vector<fs::path> subdirs;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        // Do not follow symlinks to directories: they may point outside or create cycles.
        if (it->is_directory(ec) && !it->is_symlink(ec)) {
            subdirs.push_back(it->path());
        }
    }

    if (!addWatch(dir)) {
        return false;
    }
    for (const auto& subdir : subdirs) {
        addWatch(subdir);
    }
    if (ec) {
        syslog(LOG_WARNING, "Error while scanning %s: %s", dir.c_str(), ec.message().c_str());
    }
    return true;
}

void Worker::removeWatchesUnder(const fs::path& dir)
{
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (isWithin(it->second, dir)) {
            inotify_rm_watch(inotifyFd_, it->first);
            it = watches_.erase(it);
        } else {
            ++it;
        }
    }
}

void Worker::removeAllWatches()
{
    for (const int wd : watches_ | std::views::keys) {
        inotify_rm_watch(inotifyFd_, wd);
    }
    watches_.clear();
}

std::size_t Worker::processEvents()
{
    if (inotifyFd_ < 0) {
        return 0;
    }

    alignas(inotify_event) std::array<char, 16 * 1024UL> buffer{};
    std::size_t processed = 0;

    while (true) {
        const ssize_t len = read(inotifyFd_, buffer.data(), buffer.size());
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            syslog(LOG_ERR, "read from inotify failed: %s", std::strerror(errno));
            break;
        }
        if (len == 0) {
            break;
        }

        for (const char* ptr = buffer.data(); ptr < buffer.data() + len;) {
            const auto* event = reinterpret_cast<const inotify_event*>(ptr);
            handleEvent(*event);
            ++processed;
            ptr += sizeof(inotify_event) + event->len;
        }
    }
    return processed;
}

void Worker::handleEvent(const inotify_event& event)
{
    if ((event.mask & IN_Q_OVERFLOW) != 0) {
        syslog(LOG_WARNING, "inotify queue overflow: some events were lost");
        return;
    }

    const auto it = watches_.find(event.wd);
    if (it == watches_.end()) {
        return;
    }

    if ((event.mask & IN_IGNORED) != 0) {
        // The watch was removed by the kernel (directory deleted or unmounted).
        watches_.erase(it);
        return;
    }

    fs::path path = it->second;
    if (event.len > 0) {
        path /= event.name;
    }
    // Without a name the event is about the watched directory itself (e.g. DELETE_SELF),
    // and the kernel does not set IN_ISDIR for such events.
    const bool isDir = (event.mask & IN_ISDIR) != 0 || event.len == 0;

    if (sink_) {
        sink_(FsEvent{path, event.mask, isDir});
    }

    if (isDir && (event.mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
        addWatchRecursive(path);
    } else if (isDir && (event.mask & IN_MOVED_FROM) != 0) {
        removeWatchesUnder(path);
    }
}

} // namespace monitor
