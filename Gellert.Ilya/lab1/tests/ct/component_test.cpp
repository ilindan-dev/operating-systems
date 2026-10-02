// Component tests: the real disk_monitor binary is started as a black box, and its behaviour
// is checked from the outside: process attributes in /proc, the PID file, reactions to signals
// and the messages it sends to syslog.
//
// They must run inside the Docker container (./run_tests.sh): they start and kill daemons,
// use the global /tmp/disk_monitor.pid and replace /dev/log.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "common/test_utils.hpp"
#include "ct/daemon_runner.hpp"
#include "ct/syslog_collector.hpp"
#include "daemon/pid_manager.hpp"

namespace fs = std::filesystem;
using monitor::PidManager;
using namespace std::chrono_literals;

namespace {

constexpr const char* kPidFile = "/tmp/disk_monitor.pid";

/// syslog priority = facility * 8 + severity; LOG_LOCAL0 is facility 16.
constexpr const char* kLocal0Info = "<134>";
constexpr const char* kLocal0Err = "<131>";

std::string fileEvent(const std::string& action, const fs::path& path)
{
    return action + " file " + path.string();
}

std::string dirEvent(const std::string& action, const fs::path& path)
{
    return action + " dir " + path.string();
}

std::string tag(pid_t pid)
{
    return "disk-monitor[" + std::to_string(pid) + "]: ";
}

auto findMessage(const std::vector<std::string>& messages, const std::string& part)
{
    return std::ranges::find_if(messages,
                                [&](const auto& message) { return test::contains(message, part); });
}

class DaemonComponentTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (std::getenv("DISK_MONITOR_IN_CONTAINER") == nullptr) {
            GTEST_SKIP() << "Component tests start real daemons, send signals and replace "
                            "/dev/log. Run them in Docker: ./run_tests.sh";
        }
        test::becomeSubreaper();
        test::killAllDaemons();
        fs::remove(kPidFile);

        syslog = std::make_unique<test::SyslogCollector>();
        watched = tmp / "watched";
        fs::create_directory(watched);
        writeConfig("DIR=watched\n");
    }

    void TearDown() override
    {
        if (!syslog) {
            return;
        }
        test::killAllDaemons();
        fs::remove(kPidFile);
    }

    fs::path configPath() const
    {
        return tmp / "disk-monitor.conf";
    }

    void writeConfig(const std::string& content) const
    {
        test::writeFile(configPath(), content);
    }

    /// Waits for a PID file with a live process other than `previous`.
    static pid_t waitForPidFile(pid_t previous = 0)
    {
        pid_t pid = 0;
        test::waitUntil([&] {
            const auto value = PidManager::readPidFile(kPidFile);
            pid = value.value_or(0);
            return pid > 0 && pid != previous && PidManager::isProcessAlive(pid);
        });
        return pid;
    }

    /// Launches the daemon with the test config and waits until it is watching `ready`.
    pid_t startDaemon(pid_t previous = 0)
    {
        const auto result = test::launchDaemon({configPath().string()}, tmp.path());
        EXPECT_EQ(result.exitCode, 0) << result.output;
        EXPECT_EQ(result.output, "");

        const pid_t pid = waitForPidFile(previous);
        EXPECT_GT(pid, 0) << "no PID file\n" << syslog->dump();
        EXPECT_TRUE(syslog->waitFor(tag(pid) + "Monitoring directory: ")) << syslog->dump();
        return pid;
    }

    static bool isAlive(pid_t pid)
    {
        test::reapZombies();
        return PidManager::isProcessAlive(pid);
    }

    static bool waitForExit(pid_t pid)
    {
        return test::waitUntil([pid] { return !isAlive(pid); });
    }

    test::TempDir tmp;
    fs::path watched;
    std::unique_ptr<test::SyslogCollector> syslog;
};

} // namespace

// ---------- Daemonization ----------

TEST_F(DaemonComponentTest, LauncherReturnsAndDaemonKeepsRunning)
{
    const pid_t pid = startDaemon();

    EXPECT_TRUE(isAlive(pid));
    EXPECT_EQ(test::findDaemons(), std::vector<pid_t>{pid}) << "exactly one process must remain";
}

TEST_F(DaemonComponentTest, DaemonIsDetachedFromTerminalAndSession)
{
    const pid_t pid = startDaemon();
    const auto stat = test::readProcStat(pid);
    ASSERT_TRUE(stat.has_value());
    const test::ProcStat& procStat = stat.value();

    EXPECT_EQ(procStat.ttyNr, 0) << "must not have a controlling terminal";
    EXPECT_NE(procStat.session, getsid(0)) << "must run in its own session (setsid)";
    EXPECT_NE(procStat.session, pid) << "must not be the session leader (second fork)";
    EXPECT_EQ(procStat.pgrp, procStat.session);
}

TEST_F(DaemonComponentTest, WorkingDirectoryStdioAndUmask)
{
    const pid_t pid = startDaemon();
    const fs::path proc = fs::path("/proc") / std::to_string(pid);

    EXPECT_EQ(test::readLink(proc / "cwd"), "/");
    for (const char* fd : {"0", "1", "2"}) {
        EXPECT_EQ(test::readLink(proc / "fd" / fd), "/dev/null") << "descriptor " << fd;
    }
    EXPECT_EQ(test::readProcStatus(pid, "Umask"), "0027");
}

TEST_F(DaemonComponentTest, LogsStartupToLocal0Facility)
{
    const pid_t pid = startDaemon();

    const std::string started =
        syslog->find(tag(pid) + "Daemon started (PID " + std::to_string(pid) +
                     "), config: " + configPath().string());
    ASSERT_FALSE(started.empty()) << syslog->dump();
    EXPECT_EQ(started.rfind(kLocal0Info, 0), 0U) << started;
    EXPECT_GT(syslog->count("Monitoring directory: " + watched.string()), 0U);
}

// ---------- File system events ----------

TEST_F(DaemonComponentTest, LogsFileOperations)
{
    startDaemon();
    const fs::path file = watched / "report.txt";
    const fs::path renamed = watched / "renamed.txt";

    test::writeFile(file, "data");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", file)));
    EXPECT_TRUE(syslog->waitFor(fileEvent("MODIFY", file)));
    EXPECT_TRUE(syslog->waitFor(fileEvent("CLOSE_WRITE", file)));

    test::readFile(file);
    EXPECT_TRUE(syslog->waitFor(fileEvent("ACCESS", file)));
    EXPECT_TRUE(syslog->waitFor(fileEvent("CLOSE_NOWRITE", file)));

    fs::rename(file, renamed);
    EXPECT_TRUE(syslog->waitFor(fileEvent("MOVED_FROM", file)));
    EXPECT_TRUE(syslog->waitFor(fileEvent("MOVED_TO", renamed)));

    fs::remove(renamed);
    EXPECT_TRUE(syslog->waitFor(fileEvent("DELETE", renamed))) << syslog->dump();
}

TEST_F(DaemonComponentTest, LogsEventsInNestedDirectories)
{
    fs::create_directories(watched / "a" / "b");
    startDaemon();

    test::writeFile(watched / "a" / "b" / "old.txt", "x");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", watched / "a" / "b" / "old.txt")));

    fs::create_directory(watched / "c");
    EXPECT_TRUE(syslog->waitFor(dirEvent("CREATE", watched / "c")));
    test::writeFile(watched / "c" / "new.txt", "x");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", watched / "c" / "new.txt"))) << syslog->dump();
}

TEST_F(DaemonComponentTest, MissingDirectoryDoesNotStopOthers)
{
    writeConfig("DIR=missing\nDIR=watched\n");
    startDaemon();

    const std::string error =
        syslog->find("Failed to watch directory " + (tmp / "missing").string());
    ASSERT_FALSE(error.empty()) << syslog->dump();
    EXPECT_EQ(error.rfind(kLocal0Err, 0), 0U) << error;

    test::writeFile(watched / "x.txt", "x");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", watched / "x.txt")));
}

// ---------- Configuration ----------

TEST_F(DaemonComponentTest, ReadsDefaultConfigFromWorkingDirectory)
{
    const auto result = test::launchDaemon({}, tmp.path());
    ASSERT_EQ(result.exitCode, 0) << result.output;

    const pid_t pid = waitForPidFile();
    EXPECT_TRUE(syslog->waitFor(tag(pid) + "Monitoring directory: " + watched.string()))
        << syslog->dump();
}

TEST_F(DaemonComponentTest, ResolvesRelativeDirectoryAgainstConfigLocation)
{
    // Launched from "/", the config says "DIR=watched": it must mean <config dir>/watched.
    const auto result = test::launchDaemon({configPath().string()}, "/");
    ASSERT_EQ(result.exitCode, 0) << result.output;

    const pid_t pid = waitForPidFile();
    EXPECT_TRUE(syslog->waitFor(tag(pid) + "Monitoring directory: " + watched.string()))
        << syslog->dump();
}

TEST_F(DaemonComponentTest, FailsWithoutConfigFile)
{
    const auto result = test::launchDaemon({(tmp / "absent.conf").string()}, tmp.path());

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_TRUE(test::contains(result.output, "Failed to open configuration file"))
        << result.output;
    EXPECT_FALSE(fs::exists(kPidFile));
    EXPECT_TRUE(test::findDaemons().empty());

    const std::string fatal = syslog->find("Fatal error: Failed to open configuration file");
    ASSERT_FALSE(fatal.empty()) << syslog->dump();
    EXPECT_EQ(fatal.rfind(kLocal0Err, 0), 0U) << fatal;
}

TEST_F(DaemonComponentTest, FailsWithInvalidConfig)
{
    writeConfig("FOLDER=/tmp\n");
    const auto result = test::launchDaemon({configPath().string()}, tmp.path());

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_TRUE(test::contains(result.output, "unknown key 'FOLDER'")) << result.output;
    EXPECT_TRUE(test::findDaemons().empty());
}

TEST_F(DaemonComponentTest, PrintsUsageOnTooManyArguments)
{
    const auto result = test::launchDaemon({"a.conf", "b.conf"}, tmp.path());

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_TRUE(test::contains(result.output, "Usage")) << result.output;
}

// ---------- Signals ----------

TEST_F(DaemonComponentTest, SighupReloadsConfigImmediately)
{
    const pid_t pid = startDaemon();
    const fs::path other = tmp / "other";
    fs::create_directory(other);
    writeConfig("DIR=" + other.string() + "\n");

    kill(pid, SIGHUP);

    // No file system activity here: the reload must not wait for an inotify event.
    EXPECT_TRUE(syslog->waitFor("Configuration reloaded successfully", 1s)) << syslog->dump();
    EXPECT_TRUE(syslog->waitFor("Monitoring directory: " + other.string()));

    test::writeFile(watched / "old.txt", "x");
    test::writeFile(other / "new.txt", "x");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", other / "new.txt")));
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(syslog->count(watched.string() + "/old.txt"), 0U) << syslog->dump();
    EXPECT_TRUE(isAlive(pid));
}

TEST_F(DaemonComponentTest, InvalidConfigOnSighupKeepsPreviousOne)
{
    const pid_t pid = startDaemon();
    writeConfig("this is not a config\n");

    kill(pid, SIGHUP);

    EXPECT_TRUE(syslog->waitFor("Failed to reload configuration")) << syslog->dump();
    EXPECT_TRUE(syslog->waitFor("Keeping the previous one"));
    EXPECT_TRUE(isAlive(pid));

    test::writeFile(watched / "still.txt", "x");
    EXPECT_TRUE(syslog->waitFor(fileEvent("CREATE", watched / "still.txt")));
}

TEST_F(DaemonComponentTest, SigtermStopsDaemonAndRemovesPidFile)
{
    const pid_t pid = startDaemon();

    kill(pid, SIGTERM);

    EXPECT_TRUE(waitForExit(pid));
    EXPECT_TRUE(syslog->waitFor(tag(pid) + "Received SIGTERM, shutting down")) << syslog->dump();
    EXPECT_TRUE(syslog->waitFor(tag(pid) + "Daemon stopped"));
    EXPECT_FALSE(fs::exists(kPidFile));
}

TEST_F(DaemonComponentTest, SigintAlsoStopsDaemon)
{
    const pid_t pid = startDaemon();

    kill(pid, SIGINT);

    EXPECT_TRUE(waitForExit(pid));
    EXPECT_TRUE(syslog->waitFor(tag(pid) + "Received SIGINT, shutting down")) << syslog->dump();
    EXPECT_FALSE(fs::exists(kPidFile));
}

// ---------- Single instance ----------

TEST_F(DaemonComponentTest, SecondLaunchReplacesRunningInstance)
{
    const pid_t first = startDaemon();
    const pid_t second = startDaemon(first);

    EXPECT_NE(second, first);
    EXPECT_TRUE(waitForExit(first));
    EXPECT_TRUE(isAlive(second));
    EXPECT_EQ(PidManager::readPidFile(kPidFile), second);
    EXPECT_TRUE(
        syslog->waitFor("Sent SIGTERM to previous instance (PID " + std::to_string(first) + ")"));

    // The old instance must be gone before the new one starts working.
    const auto messages = syslog->messages();
    const auto stopped = findMessage(messages, tag(first) + "Daemon stopped");
    const auto started = findMessage(messages, tag(second) + "Daemon started");
    ASSERT_NE(stopped, messages.end()) << syslog->dump();
    ASSERT_NE(started, messages.end()) << syslog->dump();
    EXPECT_LT(stopped, started);
}

TEST_F(DaemonComponentTest, StalePidFileIsIgnored)
{
    const pid_t dead = test::deadPid();
    test::writeFile(kPidFile, std::to_string(dead) + "\n");

    const pid_t pid = startDaemon(dead);

    EXPECT_TRUE(isAlive(pid));
    EXPECT_TRUE(syslog->waitFor("Stale PID file")) << syslog->dump();
}

TEST_F(DaemonComponentTest, ForeignProcessInPidFileIsNotKilled)
{
    auto foreign = test::ChildProcess::foreign();
    ASSERT_TRUE(test::waitUntil([&] { return PidManager::processName(foreign.pid()) == "sleep"; }));
    test::writeFile(kPidFile, std::to_string(foreign.pid()) + "\n");

    startDaemon(foreign.pid());

    EXPECT_TRUE(PidManager::isProcessAlive(foreign.pid()));
    EXPECT_TRUE(syslog->waitFor("belongs to 'sleep'")) << syslog->dump();
}
