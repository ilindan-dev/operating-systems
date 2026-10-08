#include <gtest/gtest.h>

#include <string>

#include "proto/message.hpp"

using namespace chat::proto;

namespace {

Message sample()
{
    return Message{MessageType::Chat, 3, kBroadcast, 1'700'000'000'123, "Привет, чат!"};
}

} // namespace

TEST(MessageTest, EncodeDecodeRoundTrip)
{
    const Message original = sample();
    const auto decoded = decode(encode(original));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, original);
}

TEST(MessageTest, RoundTripOfEveryType)
{
    for (const auto type :
         {MessageType::Hello, MessageType::Chat, MessageType::Joined, MessageType::Left}) {
        Message message = sample();
        message.type = type;
        EXPECT_EQ(decode(encode(message)), message);
    }
}

TEST(MessageTest, ForwardedAuthorSurvivesRoundTrip)
{
    Message message = sample();
    message.forwardedFrom = 7;
    const auto decoded = decode(encode(message));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->forwardedFrom, 7U);
}

TEST(MessageTest, NotForwardedByDefault)
{
    EXPECT_EQ(Message{}.forwardedFrom, kNobody);
}

TEST(MessageTest, EmptyTextIsAllowed)
{
    Message message = sample();
    message.text.clear();
    EXPECT_EQ(decode(encode(message)), message);
}

TEST(MessageTest, TruncatedBytesAreRejected)
{
    const auto bytes = encode(sample());
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(decode(std::span(bytes).first(size)).has_value()) << "size " << size;
    }
}

TEST(MessageTest, TrailingBytesAreRejected)
{
    auto bytes = encode(sample());
    bytes.push_back(std::byte{0});
    EXPECT_FALSE(decode(bytes).has_value());
}

TEST(MessageTest, UnknownTypeIsRejected)
{
    auto bytes = encode(sample());
    bytes[0] = std::byte{42};
    EXPECT_FALSE(decode(bytes).has_value());
}

TEST(MessageTest, TooLongTextIsRejected)
{
    Message message = sample();
    message.text.assign(kMaxTextSize + 1, 'x');
    EXPECT_FALSE(decode(encode(message)).has_value());
}

TEST(MessageTest, NowIsMillisecondsSinceEpoch)
{
    EXPECT_GT(nowMs(), 1'600'000'000'000); // after 2020
}
