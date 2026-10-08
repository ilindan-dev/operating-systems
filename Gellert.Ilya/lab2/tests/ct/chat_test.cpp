// The whole chat without the GUI, once per transport (chat_tests_fifo, ...):
// a Hub runs in the test process, every client is a separate forked process,
// so the handshake, the channels and SIGKILL work exactly as between the real binaries.
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "client/model/client_session.hpp"
#include "client/model/handshake.hpp"
#include "common/test_utils.hpp"
#include "host/model/hub.hpp"
#include "posix/io.hpp"
#include "posix/signals.hpp"

using namespace chat;
using namespace std::chrono_literals;
using proto::kBroadcast;
using proto::kHostId;
using proto::Message;
using proto::ParticipantId;

namespace {

/// Collects what the Hub reports to the host's window.
class HostEvents : public host::HubListener {
public:
    void onJoined(ParticipantId id, const std::string& name, pid_t) override
    {
        log_.update([&] { joined_[id] = name; });
    }
    void onLeft(ParticipantId id, const std::string&, const std::string& reason) override
    {
        log_.update([&] { left_[id] = reason; });
    }
    void onMessage(const Message& message) override
    {
        log_.update([&] { messages_.push_back(message); });
    }

    std::optional<ParticipantId> waitJoined(const std::string& name)
    {
        std::optional<ParticipantId> id;
        log_.waitFor([&] {
            const auto it = std::ranges::find_if(
                joined_, [&](const auto& entry) { return entry.second == name; });
            if (it != joined_.end()) {
                id = it->first;
            }
            return id.has_value();
        });
        return id;
    }
    std::optional<std::string> waitLeft(ParticipantId id, std::chrono::milliseconds timeout = 5s)
    {
        std::optional<std::string> reason;
        log_.waitFor(
            [&] {
                if (left_.contains(id)) {
                    reason = left_[id];
                }
                return reason.has_value();
            },
            timeout);
        return reason;
    }
    bool waitMessage(const std::string& text)
    {
        return log_.waitFor([&] {
            return std::ranges::any_of(messages_, [&](const Message& m) { return m.text == text; });
        });
    }
    ParticipantId forwardedFromOf(const std::string& text)
    {
        return log_.read([&] {
            const auto it =
                std::ranges::find_if(messages_, [&](const Message& m) { return m.text == text; });
            return it == messages_.end() ? proto::kNobody : it->forwardedFrom;
        });
    }
    bool sawMessage(const std::string& text)
    {
        return log_.read([&] {
            return std::ranges::any_of(messages_, [&](const Message& m) { return m.text == text; });
        });
    }

private:
    test::EventLog log_;
    std::map<ParticipantId, std::string> joined_;
    std::map<ParticipantId, std::string> left_;
    std::vector<Message> messages_;
};

/// A chat client living in a child process; methods return false instead of asserting.
class Client : public client::ClientListener {
public:
    explicit Client(const std::string& name) : session_(*this, getppid(), name) {}

    bool connect()
    {
        try {
            session_.connect();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void onJoined(ParticipantId id, const std::string& name) override
    {
        log_.update([&] { joined_[id] = name; });
    }
    void onLeft(ParticipantId id, const std::string&) override
    {
        log_.update([&] { left_.push_back(id); });
    }
    void onMessage(const Message& message) override
    {
        log_.update([&] { messages_.push_back(message); });
    }
    void onDisconnected(const std::string&) override
    {
        log_.update([&] { disconnected_ = true; });
    }

    std::optional<ParticipantId> waitJoined(const std::string& name)
    {
        std::optional<ParticipantId> id;
        log_.waitFor([&] {
            for (const auto& [participant, participantName] : joined_) {
                if (participantName == name) {
                    id = participant;
                }
            }
            return id.has_value();
        });
        return id;
    }
    std::optional<Message> waitMessage(const std::string& text,
                                       std::chrono::milliseconds timeout = 5s)
    {
        std::optional<Message> found;
        log_.waitFor(
            [&] {
                const auto it = std::ranges::find_if(
                    messages_, [&](const Message& m) { return m.text == text; });
                if (it != messages_.end()) {
                    found = *it;
                }
                return found.has_value();
            },
            timeout);
        return found;
    }
    bool waitForwardedCount(const std::string& text, std::size_t count)
    {
        return log_.waitFor([&] {
            return static_cast<std::size_t>(std::ranges::count_if(messages_, [&](const Message& m) {
                       return m.text == text && m.forwardedFrom != proto::kNobody;
                   })) >= count;
        });
    }
    bool waitLeft(ParticipantId id)
    {
        return log_.waitFor([&] { return std::ranges::find(left_, id) != left_.end(); });
    }
    bool waitDisconnected()
    {
        return log_.waitFor([&] { return disconnected_; });
    }

    client::ClientSession& session()
    {
        return session_;
    }

private:
    test::EventLog log_;
    std::map<ParticipantId, std::string> joined_;
    std::vector<ParticipantId> left_;
    std::vector<Message> messages_;
    bool disconnected_ = false;
    client::ClientSession session_;
};

/**
 * SIGUSR1 is blocked before anything else (children inherit it), clients are forked
 * before the Hub starts its threads, then released with go().
 */
class ChatTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        posix::ignoreSigpipe();
    }

    void startHub(host::HubOptions options = {})
    {
        hub_ = std::make_unique<host::Hub>(events_, std::move(options));
        hub_->start();
    }

    posix::SignalBlocker blocker_{proto::kHandshakeSignal};
    HostEvents events_;
    std::unique_ptr<host::Hub> hub_;
};

} // namespace

TEST_F(ChatTest, ClientJoinsAndBroadcastReachesHost)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        if (!client.connect()) {
            return 1;
        }
        if (!client.session().send(kBroadcast, "hello, everyone")) {
            return 2;
        }
        // Broadcasts come back to the sender too: that is how it sees them in the common order.
        return client.waitMessage("hello, everyone") ? 0 : 3;
    });
    startHub();
    alice.go();

    const auto id = events_.waitJoined("Alice");
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(*id, 1U);
    EXPECT_TRUE(events_.waitMessage("hello, everyone"));
    EXPECT_EQ(alice.exitCode(), 0);
    EXPECT_TRUE(events_.waitLeft(*id).has_value());
}

TEST_F(ChatTest, NewcomerLearnsWhoIsAlreadyInChat)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        return client.connect() && client.waitMessage("done", 10s) ? 0 : 1;
    });
    test::ChildProcess bob([] {
        Client client("Bob");
        if (!client.connect()) {
            return 1;
        }
        const auto host = client.waitJoined("Хост");
        const auto alice = client.waitJoined("Alice");
        return host == kHostId && alice.has_value() ? 0 : 2;
    });
    startHub();
    alice.go();
    ASSERT_TRUE(events_.waitJoined("Alice"));
    bob.go();

    EXPECT_EQ(bob.exitCode(), 0);
    hub_->sendFromHost(kBroadcast, "done");
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, HostMessageReachesClient)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        if (!client.connect()) {
            return 1;
        }
        const auto message = client.waitMessage("hi from host");
        return message && message->from == kHostId ? 0 : 2;
    });
    startHub();
    alice.go();
    ASSERT_TRUE(events_.waitJoined("Alice"));

    hub_->sendFromHost(kBroadcast, "hi from host");
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, PrivateMessageIsSeenOnlyBySenderAndAddressee)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        if (!client.connect()) {
            return 1;
        }
        const auto bob = client.waitJoined("Bob");
        if (!bob || !client.session().send(*bob, "secret for Bob")) {
            return 2;
        }
        const auto echo = client.waitMessage("secret for Bob"); // the sender's own copy
        return echo && echo->to == *bob ? 0 : 3;
    });
    test::ChildProcess bob([] {
        Client client("Bob");
        if (!client.connect()) {
            return 1;
        }
        const auto message = client.waitMessage("secret for Bob");
        return message && message->to == client.session().id() ? 0 : 2;
    });
    startHub();
    alice.go();
    ASSERT_TRUE(events_.waitJoined("Alice"));
    bob.go();

    EXPECT_EQ(alice.exitCode(), 0);
    EXPECT_EQ(bob.exitCode(), 0);
    EXPECT_FALSE(events_.sawMessage("secret for Bob")); // the host is not a party to it
}

TEST_F(ChatTest, PrivateMessageToHost)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        return client.connect() && client.session().send(kHostId, "only for host") &&
                       client.waitMessage("only for host")
                   ? 0
                   : 1;
    });
    startHub();
    alice.go();

    EXPECT_TRUE(events_.waitMessage("only for host"));
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, HostForwardsMessageToCommonChatAndPrivately)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        if (!client.connect() || !client.session().send(kHostId, "please pass it on")) {
            return 1;
        }
        // Alice sees the forwarded copy in the common chat, from the host, with her as the author.
        const auto copy = client.waitMessage("please pass it on");
        if (!copy) {
            return 2;
        }
        return client.waitForwardedCount("please pass it on", 1) ? 0 : 3;
    });
    test::ChildProcess bob([] {
        Client client("Bob");
        if (!client.connect()) {
            return 1;
        }
        const auto alice = client.waitJoined("Alice");
        const auto message = client.waitMessage("please pass it on");
        if (!alice || !message) {
            return 2;
        }
        return message->from == kHostId && message->forwardedFrom == *alice &&
                       message->to == client.session().id()
                   ? 0
                   : 3;
    });
    startHub();
    alice.go();
    const auto aliceId = events_.waitJoined("Alice");
    ASSERT_TRUE(aliceId.has_value());
    bob.go();
    const auto bobId = events_.waitJoined("Bob");
    ASSERT_TRUE(bobId.has_value());
    ASSERT_TRUE(events_.waitMessage("please pass it on")); // Alice → host, privately

    hub_->forward(*aliceId, "please pass it on", *bobId);     // to Bob only
    hub_->forward(*aliceId, "please pass it on", kBroadcast); // and to everyone

    EXPECT_EQ(bob.exitCode(), 0);
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, ClientCannotFakeForwarding)
{
    test::ChildProcess mallory([] {
        Client client("Mallory");
        return client.connect() && client.session().send(kBroadcast, "trust me") ? 0 : 1;
    });
    startHub();
    mallory.go();
    ASSERT_TRUE(events_.waitMessage("trust me"));
    EXPECT_TRUE(events_.forwardedFromOf("trust me") == proto::kNobody);
    EXPECT_EQ(mallory.exitCode(), 0);
}

TEST_F(ChatTest, OthersAreToldWhenSomeoneLeaves)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        if (!client.connect()) {
            return 1;
        }
        const auto bob = client.waitJoined("Bob");
        return bob && client.waitLeft(*bob) ? 0 : 2;
    });
    test::ChildProcess bob([] {
        Client client("Bob");
        return client.connect() ? 0 : 1; // and leaves at once
    });
    startHub();
    alice.go();
    ASSERT_TRUE(events_.waitJoined("Alice"));
    bob.go();

    EXPECT_EQ(bob.exitCode(), 0);
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, SilentClientIsKilled)
{
    test::ChildProcess quiet([] {
        Client client("Quiet");
        if (!client.connect()) {
            return 1;
        }
        sleep(30); // never writes anything
        return 2;
    });
    startHub({.hostName = "Хост", .inactivityLimit = 1s});
    quiet.go();

    const auto id = events_.waitJoined("Quiet");
    ASSERT_TRUE(id.has_value());
    const int status = quiet.wait(10s);
    ASSERT_NE(status, -1) << "the client was not killed";
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);

    const auto reason = events_.waitLeft(*id);
    ASSERT_TRUE(reason.has_value());
    EXPECT_NE(reason->find("молчал"), std::string::npos);
}

TEST_F(ChatTest, WritingKeepsClientAlive)
{
    test::ChildProcess chatty([] {
        Client client("Chatty");
        if (!client.connect()) {
            return 1;
        }
        for (int i = 0; i < 6; ++i) { // 3 s in total with a 1 s limit
            std::this_thread::sleep_for(500ms);
            if (!client.session().send(kBroadcast, "still here " + std::to_string(i))) {
                return 2;
            }
        }
        return client.waitMessage("still here 5") ? 0 : 3;
    });
    startHub({.hostName = "Хост", .inactivityLimit = 1s});
    chatty.go();

    EXPECT_EQ(chatty.exitCode(), 0);
}

TEST_F(ChatTest, ClientsAreDisconnectedWhenHostStops)
{
    test::ChildProcess alice([] {
        Client client("Alice");
        return client.connect() && client.waitDisconnected() ? 0 : 1;
    });
    startHub();
    alice.go();
    ASSERT_TRUE(events_.waitJoined("Alice"));

    hub_->stop();
    EXPECT_EQ(alice.exitCode(), 0);
}

TEST_F(ChatTest, ClientIdsAreReused)
{
    test::ChildProcess first([] {
        Client client("First");
        return client.connect() ? 0 : 1;
    });
    test::ChildProcess second([] {
        Client client("Second");
        return client.connect() && client.session().id() == 1 ? 0 : 1;
    });
    startHub();
    first.go();
    const auto firstId = events_.waitJoined("First");
    ASSERT_TRUE(firstId.has_value());
    EXPECT_EQ(first.exitCode(), 0);
    ASSERT_TRUE(events_.waitLeft(*firstId));

    std::this_thread::sleep_for(600ms); // the watchdog releases the old channel
    second.go();
    EXPECT_EQ(second.exitCode(), 0);
}

TEST(HandshakeTest, MissingHostIsReported)
{
    const posix::SignalBlocker blocker{proto::kHandshakeSignal};
    const pid_t child = fork();
    if (child == 0) {
        _exit(0);
    }
    waitpid(child, nullptr, 0);
    EXPECT_THROW(static_cast<void>(client::knock(child)), std::runtime_error);
}

TEST(HandshakeTest, SilentHostTimesOut)
{
    // A live process that blocks SIGUSR1 and never answers: our parent, the test itself.
    const posix::SignalBlocker blocker{proto::kHandshakeSignal};
    test::ChildProcess knocker([] {
        try {
            static_cast<void>(client::knock(getppid(), 1));
        } catch (const std::runtime_error&) {
            return 0;
        }
        return 1;
    });
    knocker.go();
    EXPECT_EQ(knocker.exitCode(std::chrono::seconds(10)), 0);
    while (posix::waitSignal({proto::kHandshakeSignal}, 0ms)) {
    }
}
