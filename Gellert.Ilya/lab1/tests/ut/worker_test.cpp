#include <gtest/gtest.h>
#include <poll.h>
#include <sys/inotify.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "common/test_utils.hpp"
#include "worker/worker.hpp"

namespace fs = std::filesystem;
using monitor::FsEvent;
using monitor::Worker;
using namespace std::chrono_literals;

// ---------- Formatting ----------

TEST(DescribeMaskTest, EmptyMask)
{
    EXPECT_EQ(monitor::describeMask(0), "NONE");
}

TEST(DescribeMaskTest, SingleEvent)
{
    EXPECT_EQ(monitor::describeMask(IN_OPEN), "OPEN");
    EXPECT_EQ(monitor::describeMask(IN_CLOSE_WRITE), "CLOSE_WRITE");
}

TEST(DescribeMaskTest, SeveralBitsInFixedOrder)
{
    EXPECT_EQ(monitor::describeMask(IN_CREATE | IN_ISDIR), "CREATE|ISDIR");
    EXPECT_EQ(monitor::describeMask(IN_CLOSE_WRITE | IN_ACCESS), "ACCESS|CLOSE_WRITE");
}

TEST(DescribeMaskTest, UnknownBitsAreShownInHex)
{
    EXPECT_EQ(monitor::describeMask(IN_OPEN | 0x00010000), "OPEN|0x10000");
}

TEST(FormatEventTest, File)
{
    EXPECT_EQ(monitor::formatEvent({"/data/a.txt", IN_MODIFY, false}), "MODIFY file /data/a.txt");
}

TEST(FormatEventTest, DirectoryWithoutIsDirInText)
{
    EXPECT_EQ(monitor::formatEvent({"/data/sub", IN_CREATE | IN_ISDIR, true}),
              "CREATE dir /data/sub");
}

// ---------- inotify ----------

namespace {

class WorkerTest : public ::testing::Test {
protected:
    test::TempDir dir;
    std::vector<FsEvent> events;
    Worker worker{[this](const FsEvent& event) { events.push_back(event); }};

    /// Processes inotify events until one with this path and mask bit arrives.
    bool waitForEvent(const fs::path& path, std::uint32_t bit,
                      std::chrono::milliseconds timeout = 2s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!hasEvent(path, bit)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            pump(20ms);
        }
        return true;
    }

    /// Processes whatever arrives during `duration`.
    void pump(std::chrono::milliseconds duration)
    {
        pollfd pfd{worker.fd(), POLLIN, 0};
        if (poll(&pfd, 1, static_cast<int>(duration.count())) > 0) {
            worker.processEvents();
        }
    }

    bool hasEvent(const fs::path& path, std::uint32_t bit) const
    {
        return std::ranges::any_of(
            events, [&](const FsEvent& e) { return e.path == path && (e.mask & bit) != 0; });
    }

    bool hasAnyEventFor(const fs::path& path) const
    {
        return std::ranges::any_of(events, [&](const FsEvent& e) { return e.path == path; });
    }
};

} // namespace

TEST_F(WorkerTest, DescriptorExistsOnlyWhileStarted)
{
    EXPECT_EQ(worker.fd(), -1);
    worker.start({dir.path()});
    EXPECT_GE(worker.fd(), 0);
    worker.stop();
    EXPECT_EQ(worker.fd(), -1);
    EXPECT_EQ(worker.watchCount(), 0U);
}

TEST_F(WorkerTest, ReportsFileLifecycle)
{
    worker.start({dir.path()});
    const fs::path file = dir / "a.txt";
    const fs::path renamed = dir / "b.txt";

    test::writeFile(file, "hello");
    EXPECT_TRUE(waitForEvent(file, IN_CREATE));
    EXPECT_TRUE(waitForEvent(file, IN_OPEN));
    EXPECT_TRUE(waitForEvent(file, IN_MODIFY));
    EXPECT_TRUE(waitForEvent(file, IN_CLOSE_WRITE));

    EXPECT_EQ(test::readFile(file), "hello");
    EXPECT_TRUE(waitForEvent(file, IN_ACCESS));
    EXPECT_TRUE(waitForEvent(file, IN_CLOSE_NOWRITE));

    fs::permissions(file, fs::perms::owner_read);
    EXPECT_TRUE(waitForEvent(file, IN_ATTRIB));

    fs::rename(file, renamed);
    EXPECT_TRUE(waitForEvent(file, IN_MOVED_FROM));
    EXPECT_TRUE(waitForEvent(renamed, IN_MOVED_TO));

    fs::remove(renamed);
    EXPECT_TRUE(waitForEvent(renamed, IN_DELETE));
}

TEST_F(WorkerTest, MarksDirectoryEvents)
{
    worker.start({dir.path()});
    fs::create_directory(dir / "sub");

    ASSERT_TRUE(waitForEvent(dir / "sub", IN_CREATE));
    const auto it =
        std::ranges::find_if(events, [&](const FsEvent& e) { return e.path == dir / "sub"; });
    EXPECT_TRUE(it->isDir);
}

TEST_F(WorkerTest, WatchesExistingSubdirectoriesRecursively)
{
    fs::create_directories(dir / "a" / "b" / "c");
    worker.start({dir.path()});
    EXPECT_EQ(worker.watchCount(), 4U);

    test::writeFile(dir / "a" / "b" / "c" / "deep.txt", "x");
    EXPECT_TRUE(waitForEvent(dir / "a" / "b" / "c" / "deep.txt", IN_CREATE));
}

TEST_F(WorkerTest, WatchesNewlyCreatedSubdirectories)
{
    worker.start({dir.path()});
    fs::create_directory(dir / "new");
    ASSERT_TRUE(waitForEvent(dir / "new", IN_CREATE));

    test::writeFile(dir / "new" / "file.txt", "x");
    EXPECT_TRUE(waitForEvent(dir / "new" / "file.txt", IN_CREATE));
}

TEST_F(WorkerTest, WatchesDirectoryTreeMovedIn)
{
    const test::TempDir outside;
    fs::create_directories(outside / "tree" / "inner");
    worker.start({dir.path()});

    fs::rename(outside / "tree", dir / "tree");
    ASSERT_TRUE(waitForEvent(dir / "tree", IN_MOVED_TO));

    test::writeFile(dir / "tree" / "inner" / "file.txt", "x");
    EXPECT_TRUE(waitForEvent(dir / "tree" / "inner" / "file.txt", IN_CREATE));
}

TEST_F(WorkerTest, StopsWatchingDirectoryMovedOut)
{
    const test::TempDir outside;
    fs::create_directories(dir / "sub");
    worker.start({dir.path()});

    fs::rename(dir / "sub", outside / "sub");
    ASSERT_TRUE(waitForEvent(dir / "sub", IN_MOVED_FROM));
    EXPECT_EQ(worker.watchCount(), 1U);

    test::writeFile(outside / "sub" / "file.txt", "x");
    pump(200ms);
    EXPECT_FALSE(hasAnyEventFor(dir / "sub" / "file.txt"));
}

TEST_F(WorkerTest, DoesNotReportItsOwnDirectoryScan)
{
    fs::create_directories(dir / "a" / "b");
    fs::create_directories(dir / "c");
    worker.start({dir.path()});

    pump(200ms);
    EXPECT_TRUE(events.empty()) << "first event: " << monitor::formatEvent(events.front());
}

TEST_F(WorkerTest, ReportsDeletionOfWatchedRoot)
{
    const fs::path root = dir / "root";
    fs::create_directory(root);
    worker.start({root});

    fs::remove(root);
    ASSERT_TRUE(waitForEvent(root, IN_DELETE_SELF));
    const auto it =
        std::ranges::find_if(events, [&](const FsEvent& e) { return e.mask & IN_DELETE_SELF; });
    EXPECT_TRUE(it->isDir);

    // The kernel drops the watch (IN_IGNORED) and the worker forgets it.
    EXPECT_TRUE(test::waitUntil([&] {
        pump(20ms);
        return worker.watchCount() == 0;
    }));
}

TEST_F(WorkerTest, SkipsMissingDirectories)
{
    EXPECT_NO_THROW(worker.start({dir / "missing", dir.path()}));
    EXPECT_EQ(worker.watchCount(), 1U);
}

TEST_F(WorkerTest, DoesNotFollowSymlinkedDirectories)
{
    const test::TempDir outside;
    fs::create_directory_symlink(outside.path(), dir / "link");
    worker.start({dir.path()});

    EXPECT_EQ(worker.watchCount(), 1U);
}

TEST_F(WorkerTest, ReloadReplacesWatchedDirectories)
{
    fs::create_directory(dir / "old");
    fs::create_directory(dir / "new");
    worker.start({dir / "old"});

    worker.reload({dir / "new"});
    test::writeFile(dir / "old" / "x.txt", "x");
    test::writeFile(dir / "new" / "y.txt", "y");

    EXPECT_TRUE(waitForEvent(dir / "new" / "y.txt", IN_CREATE));
    pump(100ms);
    EXPECT_FALSE(hasAnyEventFor(dir / "old" / "x.txt"));
}

TEST_F(WorkerTest, ProcessesManyEventsAtOnce)
{
    constexpr int kFiles = 300;
    worker.start({dir.path()});
    for (int i = 0; i < kFiles; ++i) {
        test::writeFile(dir / ("f" + std::to_string(i)), "x");
    }

    EXPECT_TRUE(test::waitUntil([&] {
        pump(20ms);
        return std::ranges::count_if(events, [](const FsEvent& e) { return e.mask & IN_CREATE; }) ==
               kFiles;
    }));
}
