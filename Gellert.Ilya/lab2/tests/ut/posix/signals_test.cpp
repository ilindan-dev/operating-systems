#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <system_error>

#include "posix/signals.hpp"

using chat::posix::sendSignal;
using chat::posix::SignalBlocker;
using chat::posix::waitSignal;
using namespace std::chrono_literals;

namespace {

bool isBlocked(int signal)
{
    sigset_t current;
    pthread_sigmask(SIG_BLOCK, nullptr, &current); // read the mask without changing it
    return sigismember(&current, signal) == 1;
}

/// Every test runs with SIGUSR1 and SIGRTMIN blocked, so a stray signal cannot kill the test
/// binary.
class SignalsTest : public ::testing::Test {
protected:
    SignalBlocker blocker{SIGUSR1, SIGRTMIN};

    void TearDown() override
    {
        // Drain whatever a failed test left pending before the blocker unblocks it.
        while (waitSignal({SIGUSR1, SIGRTMIN}, 0ms)) {
        }
    }
};

/// pid of a process that has already exited and been reaped.
pid_t deadPid()
{
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(0);
    }
    waitpid(pid, nullptr, 0);
    return pid;
}

} // namespace

TEST(SignalBlockerTest, BlocksInScopeAndRestoresAfter)
{
    ASSERT_FALSE(isBlocked(SIGUSR2));
    {
        const SignalBlocker blocker{SIGUSR2};
        EXPECT_TRUE(isBlocked(SIGUSR2));
    }
    EXPECT_FALSE(isBlocked(SIGUSR2));
}

TEST_F(SignalsTest, ReceivesSignalWithValueAndSenderPid)
{
    sendSignal(getpid(), SIGUSR1, 42);

    const auto info = waitSignal({SIGUSR1}, 1s);

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->signal, SIGUSR1);
    EXPECT_EQ(info->senderPid, getpid());
    EXPECT_EQ(info->value, 42);
}

TEST_F(SignalsTest, TimesOutWhenNothingArrives)
{
    const auto start = std::chrono::steady_clock::now();

    const auto info = waitSignal({SIGUSR1}, 100ms);

    EXPECT_FALSE(info.has_value());
    EXPECT_GE(std::chrono::steady_clock::now() - start, 100ms);
}

TEST_F(SignalsTest, ReportsWhichOfSeveralSignalsArrived)
{
    sendSignal(getpid(), SIGRTMIN, 7);

    const auto info = waitSignal({SIGUSR1, SIGRTMIN}, 1s);

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->signal, SIGRTMIN);
    EXPECT_EQ(info->value, 7);
}

// This is the handshake in miniature: another process knocks, we learn its pid.
TEST_F(SignalsTest, LearnsPidOfAnotherProcess)
{
    const pid_t parent = getpid();
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        sigval value{};
        value.sival_int = 1;
        _exit(sigqueue(parent, SIGUSR1, value) == 0 ? 0 : 1);
    }

    const auto info = waitSignal({SIGUSR1}, 2s);
    waitpid(child, nullptr, 0);

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->senderPid, child);
    EXPECT_EQ(info->value, 1);
}

// Why two clients knocking at the same moment can lose a handshake.
TEST_F(SignalsTest, StandardSignalsAreNotQueued)
{
    sendSignal(getpid(), SIGUSR1, 1);
    sendSignal(getpid(), SIGUSR1, 2); // merged into the pending one

    EXPECT_TRUE(waitSignal({SIGUSR1}, 100ms).has_value());
    EXPECT_FALSE(waitSignal({SIGUSR1}, 100ms).has_value());
}

TEST_F(SignalsTest, RealTimeSignalsAreQueuedInOrder)
{
    sendSignal(getpid(), SIGRTMIN, 1);
    sendSignal(getpid(), SIGRTMIN, 2);

    const auto first = waitSignal({SIGRTMIN}, 100ms);
    const auto second = waitSignal({SIGRTMIN}, 100ms);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->value, 1);
    EXPECT_EQ(second->value, 2);
}

TEST(SendSignalTest, ThrowsForMissingProcess)
{
    EXPECT_THROW(sendSignal(deadPid(), SIGUSR1), std::system_error);
}
