#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <string>
#include <system_error>

#include "common/test_utils.hpp"
#include "daemon/pid_manager.hpp"

namespace fs = std::filesystem;
using monitor::PidManager;
using namespace std::chrono_literals;


namespace {

class PidManagerTest : public ::testing::Test {
protected:
    test::TempDir dir;
    fs::path pidFile = dir / "test.pid";
};

} // namespace

// ---------- PID file ----------

TEST_F(PidManagerTest, ReadReturnsNulloptForMissingFile)
{
    EXPECT_FALSE(PidManager::readPidFile(pidFile).has_value());
}

TEST_F(PidManagerTest, ReadParsesPid)
{
    test::writeFile(pidFile, "1234\n");
    EXPECT_EQ(PidManager::readPidFile(pidFile), 1234);
}

TEST_F(PidManagerTest, ReadRejectsInvalidContent)
{
    for (const char* content : {"", "abc", "0", "-5", "\n"}) {
        test::writeFile(pidFile, content);
        EXPECT_FALSE(PidManager::readPidFile(pidFile).has_value())
            << "content: '" << content << "'";
    }
}

TEST_F(PidManagerTest, WriteStoresOwnPid)
{
    PidManager::writePidFile(pidFile);
    EXPECT_EQ(test::readFile(pidFile), std::to_string(getpid()) + "\n");
}

TEST_F(PidManagerTest, WriteReplacesOldContent)
{
    test::writeFile(pidFile, "99999999999999999999 garbage\n");
    PidManager::writePidFile(pidFile);
    EXPECT_EQ(PidManager::readPidFile(pidFile), getpid());
}

TEST_F(PidManagerTest, WriteThrowsIfDirectoryDoesNotExist)
{
    EXPECT_THROW(PidManager::writePidFile(dir / "no" / "such" / "dir.pid"), std::system_error);
}

TEST_F(PidManagerTest, RemoveDeletesOwnPidFile)
{
    PidManager::writePidFile(pidFile);
    PidManager::removePidFileIfOwned(pidFile);
    EXPECT_FALSE(fs::exists(pidFile));
}

TEST_F(PidManagerTest, RemoveKeepsPidFileOfAnotherProcess)
{
    // Situation: a new instance has already written its PID while the old one is exiting.
    test::writeFile(pidFile, std::to_string(getpid() + 1) + "\n");
    PidManager::removePidFileIfOwned(pidFile);
    EXPECT_TRUE(fs::exists(pidFile));
}

TEST_F(PidManagerTest, RemoveIgnoresMissingFile)
{
    EXPECT_NO_THROW(PidManager::removePidFileIfOwned(pidFile));
}

// ---------- /proc checks ----------

TEST(ProcessInfoTest, CurrentProcessIsAlive)
{
    EXPECT_TRUE(PidManager::isProcessAlive(getpid()));
}

TEST(ProcessInfoTest, ExitedProcessIsNotAlive)
{
    EXPECT_FALSE(PidManager::isProcessAlive(test::deadPid()));
}

TEST(ProcessInfoTest, ZombieIsNotAlive)
{
    test::ChildProcess child([](const std::function<void()>& ready) {
        ready();
        _exit(0);
    });
    const fs::path procDir = fs::path("/proc") / std::to_string(child.pid());

    // The child has exited but is not reaped yet: /proc/<pid> still exists.
    EXPECT_TRUE(test::waitUntil([&] { return !PidManager::isProcessAlive(child.pid()); }));
    EXPECT_TRUE(fs::exists(procDir));
    child.wait();
}

TEST(ProcessInfoTest, ProcessNameComesFromProcComm)
{
    std::string expected = test::readFile("/proc/self/comm");
    expected.pop_back();
    EXPECT_EQ(PidManager::processName(getpid()), expected);

    auto foreign = test::ChildProcess::foreign();
    EXPECT_TRUE(test::waitUntil([&] { return PidManager::processName(foreign.pid()) == "sleep"; }));
}

TEST(ProcessInfoTest, ProcessNameOfMissingProcessIsEmpty)
{
    EXPECT_EQ(PidManager::processName(test::deadPid()), "");
}

// ---------- Stopping the previous instance ----------

TEST_F(PidManagerTest, TerminateDoesNothingWithoutPidFile)
{
    EXPECT_FALSE(PidManager::terminateRunningInstance(pidFile));
}

TEST_F(PidManagerTest, TerminateIgnoresStalePid)
{
    test::writeFile(pidFile, std::to_string(test::deadPid()));
    EXPECT_FALSE(PidManager::terminateRunningInstance(pidFile));
}

TEST_F(PidManagerTest, TerminateNeverKillsItself)
{
    PidManager::writePidFile(pidFile);
    EXPECT_FALSE(PidManager::terminateRunningInstance(pidFile));
}

TEST_F(PidManagerTest, TerminateLeavesForeignProgramAlone)
{
    auto foreign = test::ChildProcess::foreign();
    ASSERT_TRUE(test::waitUntil([&] { return PidManager::processName(foreign.pid()) == "sleep"; }));
    test::writeFile(pidFile, std::to_string(foreign.pid()));

    EXPECT_FALSE(PidManager::terminateRunningInstance(pidFile));
    EXPECT_TRUE(PidManager::isProcessAlive(foreign.pid()));
}

TEST_F(PidManagerTest, TerminateStopsPreviousInstanceWithSigterm)
{
    // A forked child has the same name as this test binary, so it looks like "our" instance.
    auto previous = test::ChildProcess::sleeping();
    test::writeFile(pidFile, std::to_string(previous.pid()));

    EXPECT_TRUE(PidManager::terminateRunningInstance(pidFile, 2s));

    const int status = previous.wait();
    ASSERT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST_F(PidManagerTest, TerminateGivesUpIfProcessIgnoresSigterm)
{
    const test::ChildProcess stubborn([](const std::function<void()>& ready) {
        signal(SIGTERM, SIG_IGN);
        ready();
        for (;;) {
            pause();
        }
    });
    test::writeFile(pidFile, std::to_string(stubborn.pid()));

    EXPECT_FALSE(PidManager::terminateRunningInstance(pidFile, 200ms));
    EXPECT_TRUE(PidManager::isProcessAlive(stubborn.pid()));
}
