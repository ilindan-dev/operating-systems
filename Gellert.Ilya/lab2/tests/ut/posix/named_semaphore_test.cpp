#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <format>
#include <string>
#include <system_error>
#include <thread>

#include "posix/named_semaphore.hpp"

using chat::posix::NamedSemaphore;
using namespace std::chrono_literals;

namespace {

std::string uniqueName(const char* tag)
{
    return std::format("/chat-test-{}-{}", getpid(), tag);
}

} // namespace

TEST(NamedSemaphoreTest, StartsWithInitialValue)
{
    auto sem = NamedSemaphore::create(uniqueName("initial"), 2);
    EXPECT_TRUE(sem.tryWait());
    EXPECT_TRUE(sem.tryWait());
    EXPECT_FALSE(sem.tryWait());
}

TEST(NamedSemaphoreTest, PostWakesWaiter)
{
    auto sem = NamedSemaphore::create(uniqueName("post"));
    std::jthread poster([&sem] {
        std::this_thread::sleep_for(50ms);
        sem.post();
    });
    EXPECT_TRUE(sem.wait(2s));
}

TEST(NamedSemaphoreTest, WaitTimesOut)
{
    auto sem = NamedSemaphore::create(uniqueName("timeout"));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(sem.wait(100ms));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 100ms);
}

TEST(NamedSemaphoreTest, OpenedByNameSharesTheValue)
{
    const std::string name = uniqueName("shared");
    auto owner = NamedSemaphore::create(name);
    auto other = NamedSemaphore::open(name);
    owner.post();
    EXPECT_TRUE(other.tryWait());
}

TEST(NamedSemaphoreTest, CreatorRemovesTheNameOnDestruction)
{
    const std::string name = uniqueName("unlink");
    {
        auto owner = NamedSemaphore::create(name);
    }
    EXPECT_THROW(NamedSemaphore::open(name), std::system_error);
}

TEST(NamedSemaphoreTest, CreateReplacesStaleSemaphore)
{
    const std::string name = uniqueName("stale");
    auto first = NamedSemaphore::create(name, 5);
    auto second = NamedSemaphore::create(name); // e.g. after a crash; starts from zero again
    EXPECT_FALSE(second.tryWait());
}

TEST(NamedSemaphoreTest, MoveKeepsOwnership)
{
    const std::string name = uniqueName("move");
    {
        auto owner = NamedSemaphore::create(name);
        NamedSemaphore moved = std::move(owner);
        auto other = NamedSemaphore::open(name);
        moved.post();
        EXPECT_TRUE(other.tryWait());
    }
    EXPECT_THROW(NamedSemaphore::open(name), std::system_error);
}
