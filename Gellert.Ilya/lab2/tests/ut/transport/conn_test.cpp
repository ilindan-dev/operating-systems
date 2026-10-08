// Built once per transport (conn_tests_fifo, conn_tests_sock, conn_tests_pipe):
// the same contract must hold for every Conn implementation.
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "posix/io.hpp"
#include "transport/conn.hpp"

using chat::transport::Conn;
using chat::transport::ConnId;
using chat::transport::ReadResult;
using namespace std::chrono_literals;

namespace {

ConnId nextId()
{
    static std::uint32_t next = 1;
    return {getpid(), next++};
}

std::vector<std::byte> bytes(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return {data, data + text.size()};
}

std::string text(const std::vector<std::byte>& data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

class ConnTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        chat::posix::ignoreSigpipe();
    }

    /// Host and client channel after the client's first message, like after a handshake.
    struct Pair {
        Conn host;
        Conn client;
    };

    static Pair connectedPair()
    {
        const ConnId id = nextId();
        Conn host(id, true);
        Conn client(id, false);
        std::vector<std::byte> got;
        EXPECT_TRUE(client.write(bytes("hello")));
        EXPECT_EQ(host.read(got, 1s), ReadResult::Ok);
        return {std::move(host), std::move(client)};
    }

    std::vector<std::byte> got;
};

} // namespace

TEST_F(ConnTest, ReportsItsType)
{
    EXPECT_EQ(Conn::typeName(), CHAT_TRANSPORT);
}

TEST_F(ConnTest, ClientToHostAndBack)
{
    const ConnId id = nextId();
    Conn host(id, true);
    Conn client(id, false);

    ASSERT_TRUE(client.write(bytes("hello")));
    ASSERT_EQ(host.read(got, 1s), ReadResult::Ok);
    EXPECT_EQ(text(got), "hello");

    ASSERT_TRUE(host.write(bytes("welcome")));
    ASSERT_EQ(client.read(got, 1s), ReadResult::Ok);
    EXPECT_EQ(text(got), "welcome");
}

TEST_F(ConnTest, HostCannotWriteBeforeClientSpeaks)
{
    Conn host(nextId(), true);
    EXPECT_FALSE(host.write(bytes("too early")));
}

TEST_F(ConnTest, ReadTimesOutWhenNothingWasSent)
{
    auto [host, client] = connectedPair();
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(host.read(got, 100ms), ReadResult::Timeout);
    EXPECT_EQ(client.read(got, 100ms), ReadResult::Timeout);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 200ms);
}

TEST_F(ConnTest, HostWaitsForClientThatHasNotConnectedYet)
{
    Conn host(nextId(), true);
    EXPECT_EQ(host.read(got, 100ms), ReadResult::Timeout);
}

TEST_F(ConnTest, KeepsMessageBoundariesAndOrder)
{
    auto [host, client] = connectedPair();
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(client.write(bytes("message " + std::to_string(i))));
    }
    for (int i = 0; i < 200; ++i) {
        ASSERT_EQ(host.read(got, 1s), ReadResult::Ok);
        EXPECT_EQ(text(got), "message " + std::to_string(i));
    }
}

TEST_F(ConnTest, CarriesEmptyAndLargeMessages)
{
    auto [host, client] = connectedPair();
    const std::string large(50'000, 'x'); // close to the pipe buffer: written in parts

    std::jthread writer([&client, &large] {
        EXPECT_TRUE(client.write(bytes("")));
        EXPECT_TRUE(client.write(bytes(large)));
    });
    ASSERT_EQ(host.read(got, 1s), ReadResult::Ok);
    EXPECT_TRUE(got.empty());
    ASSERT_EQ(host.read(got, 2s), ReadResult::Ok);
    EXPECT_EQ(text(got), large);
}

TEST_F(ConnTest, HostSeesClientLeaving)
{
    auto pair = connectedPair();
    ASSERT_TRUE(pair.client.write(bytes("bye")));
    {
        Conn gone = std::move(pair.client);
    }
    ASSERT_EQ(pair.host.read(got, 1s),
              ReadResult::Ok); // what was sent before leaving still arrives
    EXPECT_EQ(text(got), "bye");
    EXPECT_EQ(pair.host.read(got, 1s), ReadResult::Closed);
}

TEST_F(ConnTest, HelloFromClientThatLeftAtOnceIsDelivered)
{
    const ConnId id = nextId();
    Conn host(id, true);
    {
        Conn client(id, false);
        ASSERT_TRUE(client.write(bytes("hello and bye")));
    }
    ASSERT_EQ(host.read(got, 1s), ReadResult::Ok);
    EXPECT_EQ(text(got), "hello and bye");
    EXPECT_EQ(host.read(got, 1s), ReadResult::Closed);
}

TEST_F(ConnTest, ClientSeesHostLeaving)
{
    auto pair = connectedPair();
    {
        Conn gone = std::move(pair.host);
    }
    EXPECT_EQ(pair.client.read(got, 1s), ReadResult::Closed);
    EXPECT_FALSE(pair.client.write(bytes("anyone?")));
}

TEST_F(ConnTest, ClientCannotOpenMissingChannel)
{
    EXPECT_THROW(Conn({getpid(), 999}, false), std::system_error);
}

TEST_F(ConnTest, HostRemovesChannelOnDestruction)
{
    const ConnId id = nextId();
    {
        Conn host(id, true);
    }
    EXPECT_THROW(Conn(id, false), std::system_error); // files and semaphores are gone
}

TEST_F(ConnTest, IndependentChannelsForDifferentClients)
{
    const ConnId first = nextId();
    const ConnId second = nextId();
    Conn host1(first, true);
    Conn host2(second, true);
    Conn client1(first, false);
    Conn client2(second, false);

    ASSERT_TRUE(client2.write(bytes("two")));
    ASSERT_TRUE(client1.write(bytes("one")));
    ASSERT_EQ(host1.read(got, 1s), ReadResult::Ok);
    EXPECT_EQ(text(got), "one");
    ASSERT_EQ(host2.read(got, 1s), ReadResult::Ok);
    EXPECT_EQ(text(got), "two");
}
