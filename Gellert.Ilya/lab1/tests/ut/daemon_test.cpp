#include <gtest/gtest.h>

#include <type_traits>

#include "daemon/daemon.hpp"

using monitor::Daemon;

TEST(DaemonTest, IsSingleton)
{
    EXPECT_EQ(&Daemon::getInstance(), &Daemon::getInstance());
}

TEST(DaemonTest, CannotBeCopied)
{
    static_assert(!std::is_copy_constructible_v<Daemon>);
    static_assert(!std::is_copy_assignable_v<Daemon>);
    static_assert(!std::is_default_constructible_v<Daemon>, "constructor must be private");
}
