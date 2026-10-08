#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "proto/constants.hpp"

namespace chat::proto {

enum class MessageType : std::uint8_t {
    Hello = 1, ///< client → host, first message after the handshake; text = name
    Chat,      ///< a chat message; to = kBroadcast or a participant id
    Joined,    ///< host → client: participant `from` named `text` is in the chat
    Left,      ///< host → client: participant `from` left; text = reason
};

struct Message {
    MessageType type = MessageType::Chat;
    ParticipantId from = kHostId;
    ParticipantId to = kBroadcast;
    std::int64_t sentAtMs = 0; ///< sender's clock, ms since the Unix epoch; chat is sorted by it
    std::string text;
    /// The host can forward someone's message: then `from` is the host and this is the author.
    ParticipantId forwardedFrom = kNobody;

    bool operator==(const Message&) const = default;
};

/// Current time in the format of Message::sentAtMs.
[[nodiscard]] std::int64_t nowMs();

/// Message → bytes: type(1) from(4) to(4) sentAt(8) forwardedFrom(4) textSize(4) text.
[[nodiscard]] std::vector<std::byte> encode(const Message& message);

/// Bytes → message; std::nullopt if the bytes are not a valid message.
[[nodiscard]] std::optional<Message> decode(std::span<const std::byte> bytes);

} // namespace chat::proto
