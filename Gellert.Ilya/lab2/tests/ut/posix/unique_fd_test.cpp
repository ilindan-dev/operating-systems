#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <type_traits>
#include <utility>

#include "posix/unique_fd.hpp"

using chat::posix::UniqueFd;

// Ownership rules are checked by the compiler, not at run time.
static_assert(!std::is_copy_constructible_v<UniqueFd>);
static_assert(!std::is_copy_assignable_v<UniqueFd>);
static_assert(std::is_nothrow_move_constructible_v<UniqueFd>);
static_assert(std::is_nothrow_move_assignable_v<UniqueFd>);

namespace {

/// True if the process has `fd` open. For a closed number fcntl fails with EBADF.
bool isOpen(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

/// A fresh open descriptor: the read end of a pipe (the write end is closed at once).
int openFd()
{
    std::array<int, 2> fds{};
    if (pipe(fds.data()) != 0) {
        ADD_FAILURE() << "pipe() failed";
        return -1;
    }
    close(fds[1]);
    return fds[0];
}

} // namespace

TEST(UniqueFdTest, DefaultConstructedIsEmpty)
{
    const UniqueFd fd;
    EXPECT_EQ(fd.get(), -1);
    EXPECT_FALSE(fd.valid());
}

TEST(UniqueFdTest, OwnsDescriptorPassedToConstructor)
{
    const int raw = openFd();
    const UniqueFd fd(raw);
    EXPECT_EQ(fd.get(), raw);
    EXPECT_TRUE(fd.valid());
}

TEST(UniqueFdTest, ClosesDescriptorOnDestruction)
{
    const int raw = openFd();
    {
        const UniqueFd fd(raw);
        EXPECT_TRUE(isOpen(raw));
    }
    EXPECT_FALSE(isOpen(raw));
}

TEST(UniqueFdTest, MoveConstructorTransfersOwnership)
{
    const int raw = openFd();
    {
        UniqueFd source(raw);
        const UniqueFd target(std::move(source));

        EXPECT_EQ(source.get(),
                  -1); // NOLINT(bugprone-use-after-move): checking the moved-from state
        EXPECT_EQ(target.get(), raw);
        EXPECT_TRUE(isOpen(raw));
    }
    EXPECT_FALSE(isOpen(raw)); // closed exactly once, by target
}

TEST(UniqueFdTest, MoveAssignmentClosesPreviousDescriptor)
{
    const int first = openFd();
    const int second = openFd();
    UniqueFd source(first);
    UniqueFd target(second);

    target = std::move(source);

    EXPECT_EQ(source.get(), -1); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(target.get(), first);
    EXPECT_TRUE(isOpen(first));
    EXPECT_FALSE(isOpen(second));
}

TEST(UniqueFdTest, SelfMoveAssignmentKeepsDescriptor)
{
    const int raw = openFd();
    UniqueFd fd(raw);
    UniqueFd& alias = fd; // through a reference, so the compiler does not reject the self-move

    fd = std::move(alias);

    EXPECT_EQ(fd.get(), raw);
    EXPECT_TRUE(isOpen(raw));
}

TEST(UniqueFdTest, ReleaseGivesUpOwnershipWithoutClosing)
{
    const int raw = openFd();
    int released = -1;
    {
        UniqueFd fd(raw);
        released = fd.release();
        EXPECT_EQ(fd.get(), -1);
    }
    EXPECT_EQ(released, raw);
    EXPECT_TRUE(isOpen(raw));
    close(raw);
}

TEST(UniqueFdTest, ResetClosesOldAndOwnsNew)
{
    const int first = openFd();
    const int second = openFd();
    UniqueFd fd(first);

    fd.reset(second);

    EXPECT_FALSE(isOpen(first));
    EXPECT_EQ(fd.get(), second);
}

TEST(UniqueFdTest, ResetWithoutArgumentLeavesEmpty)
{
    const int raw = openFd();
    UniqueFd fd(raw);

    fd.reset();

    EXPECT_FALSE(isOpen(raw));
    EXPECT_FALSE(fd.valid());
}
