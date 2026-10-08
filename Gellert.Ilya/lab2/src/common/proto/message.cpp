#include "message.hpp"

#include <chrono>
#include <cstring>

namespace chat::proto {

namespace {

template <typename T> void put(std::vector<std::byte>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/// Reads values one after another and remembers if the input ran out.
class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename T> bool get(T& value)
    {
        if (bytes_.size() < sizeof(T)) {
            return false;
        }
        std::memcpy(&value, bytes_.data(), sizeof(T));
        bytes_ = bytes_.subspan(sizeof(T));
        return true;
    }

    bool getString(std::string& value, std::size_t size)
    {
        if (bytes_.size() < size) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data()), size);
        bytes_ = bytes_.subspan(size);
        return true;
    }

    [[nodiscard]] bool empty() const
    {
        return bytes_.empty();
    }

private:
    std::span<const std::byte> bytes_;
};

bool isKnownType(std::uint8_t type)
{
    return type >= static_cast<std::uint8_t>(MessageType::Hello) &&
           type <= static_cast<std::uint8_t>(MessageType::Left);
}

} // namespace

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::byte> encode(const Message& message)
{
    std::vector<std::byte> out;
    out.reserve(25 + message.text.size());
    put(out, static_cast<std::uint8_t>(message.type));
    put(out, message.from);
    put(out, message.to);
    put(out, message.sentAtMs);
    put(out, message.forwardedFrom);
    put(out, static_cast<std::uint32_t>(message.text.size()));
    const auto* text = reinterpret_cast<const std::byte*>(message.text.data());
    out.insert(out.end(), text, text + message.text.size());
    return out;
}

std::optional<Message> decode(std::span<const std::byte> bytes)
{
    Reader reader(bytes);
    Message message;
    std::uint8_t type = 0;
    std::uint32_t textSize = 0;
    if (!reader.get(type) || !isKnownType(type) || !reader.get(message.from) ||
        !reader.get(message.to) || !reader.get(message.sentAtMs) ||
        !reader.get(message.forwardedFrom) || !reader.get(textSize) || textSize > kMaxTextSize ||
        !reader.getString(message.text, textSize) || !reader.empty()) {
        return std::nullopt;
    }
    message.type = static_cast<MessageType>(type);
    return message;
}

} // namespace chat::proto
