#include "client_session.hpp"

#include <chrono>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "client/model/handshake.hpp"
#include "posix/log.hpp"

namespace chat::client {

namespace {

using proto::Message;
using proto::MessageType;

constexpr std::chrono::milliseconds kPollInterval{250};

} // namespace

ClientSession::ClientSession(ClientListener& listener, pid_t hostPid, std::string name)
    : listener_(listener), hostPid_(hostPid), name_(std::move(name))
{}

ClientSession::~ClientSession()
{
    stop();
}

void ClientSession::connect()
{
    id_ = knock(hostPid_);
    try {
        conn_.emplace(transport::ConnId{hostPid_, id_}, false);
    } catch (const std::system_error& e) {
        throw std::runtime_error(std::format("Не удалось открыть канал ({}): {}",
                                             transport::Conn::typeName(), e.what()));
    }
    if (!write(Message{MessageType::Hello, id_, proto::kHostId, proto::nowMs(), name_})) {
        throw std::runtime_error("Хост закрыл канал сразу после подключения");
    }
    log::info(std::format("connected to host {} as {} '{}' over {}", hostPid_, id_, name_,
                          transport::Conn::typeName()));
    reader_ = std::jthread([this](const std::stop_token& stop) { readLoop(stop); });
}

void ClientSession::stop()
{
    if (reader_.joinable()) {
        reader_.request_stop();
        reader_.join();
    }
    conn_.reset();
}

bool ClientSession::send(proto::ParticipantId to, std::string text)
{
    if (text.empty() || text.size() > proto::kMaxTextSize) {
        return false;
    }
    return write(Message{MessageType::Chat, id_, to, proto::nowMs(), std::move(text)});
}

bool ClientSession::write(const Message& message)
{
    const auto bytes = proto::encode(message);
    const std::lock_guard lock(writeMutex_);
    return conn_ && conn_->write(bytes);
}

void ClientSession::readLoop(const std::stop_token& stop)
{
    std::vector<std::byte> bytes;
    while (!stop.stop_requested()) {
        transport::ReadResult result{};
        try {
            result = conn_->read(bytes, kPollInterval);
        } catch (const std::system_error& e) {
            listener_.onDisconnected(std::format("ошибка канала: {}", e.what()));
            return;
        }
        if (result == transport::ReadResult::Timeout) {
            continue;
        }
        if (result == transport::ReadResult::Closed) {
            log::info("the host closed the channel");
            listener_.onDisconnected("хост завершил чат");
            return;
        }

        const auto message = proto::decode(bytes);
        if (!message) {
            log::warning("malformed message from the host dropped");
            continue;
        }
        switch (message->type) {
        case MessageType::Joined:
            listener_.onJoined(message->from, message->text);
            break;
        case MessageType::Left:
            listener_.onLeft(message->from, message->text);
            break;
        case MessageType::Chat:
            listener_.onMessage(*message);
            break;
        case MessageType::Hello:
            break;
        }
    }
}

proto::ParticipantId ClientSession::id() const noexcept
{
    return id_;
}

const std::string& ClientSession::name() const noexcept
{
    return name_;
}

pid_t ClientSession::hostPid() const noexcept
{
    return hostPid_;
}

} // namespace chat::client
