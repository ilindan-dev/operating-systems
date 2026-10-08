#include "hub.hpp"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <format>
#include <system_error>
#include <utility>

#include "posix/log.hpp"
#include "transport/conn.hpp"

namespace chat::host {

namespace {

using proto::Message;
using proto::MessageType;
using proto::ParticipantId;

/// Readers and the watchdog wake up this often to notice a stop request.
constexpr std::chrono::milliseconds kPollInterval{250};

/// Trims spaces and cuts the name to kMaxNameSize bytes without splitting a UTF-8 character.
std::string cleanName(std::string name, ParticipantId id)
{
    const auto first = name.find_first_not_of(" \t\r\n");
    const auto last = name.find_last_not_of(" \t\r\n");
    name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
    if (name.size() > proto::kMaxNameSize) {
        std::size_t cut = proto::kMaxNameSize;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
            --cut; // do not stop in the middle of a multi-byte character
        }
        name.resize(cut);
    }
    return name.empty() ? std::format("Гость {}", id) : name;
}

Message event(MessageType type, ParticipantId from, std::string text)
{
    return Message{type, from, proto::kBroadcast, proto::nowMs(), std::move(text)};
}

} // namespace

Hub::Hub(HubListener& listener, HubOptions options)
    : listener_(listener), options_(std::move(options)),
      handshake_([this](pid_t pid) { return acceptClient(pid); })
{}

Hub::~Hub()
{
    stop();
}

void Hub::start()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = false;
    }
    handshake_.start();
    watchdog_ = std::jthread([this](const std::stop_token& stop) { watchdogLoop(stop); });
    log::info(std::format("chat started, clients connect to pid {}", getpid()));
}

void Hub::stop()
{
    handshake_.stop();
    if (watchdog_.joinable()) {
        watchdog_.request_stop();
        watchdog_.join();
    }

    std::map<ParticipantId, std::unique_ptr<Participant>> active;
    std::vector<std::unique_ptr<Participant>> finished;
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        active.swap(participants_);
        finished.swap(finished_);
    }
    // Destroying a Participant stops and joins its reader thread, then closes its channel,
    // so every client sees the end of the stream. Done without the lock: the readers take it.
    active.clear();
    finished.clear();
}

void Hub::sendFromHost(ParticipantId to, std::string text)
{
    if (text.empty() || text.size() > proto::kMaxTextSize) {
        return;
    }
    route(Message{MessageType::Chat, proto::kHostId, to, proto::nowMs(), std::move(text)});
}

void Hub::forward(ParticipantId author, std::string text, ParticipantId to)
{
    if (text.empty() || text.size() > proto::kMaxTextSize) {
        return;
    }
    Message message{MessageType::Chat, proto::kHostId, to, proto::nowMs(), std::move(text)};
    message.forwardedFrom = author;
    log::info(std::format("host forwards a message of {} to {}", author, to));
    route(message);
}

std::vector<ParticipantInfo> Hub::participants() const
{
    const std::lock_guard lock(mutex_);
    std::vector<ParticipantInfo> result;
    for (const auto& [id, participant] : participants_) {
        if (participant->joined) {
            result.push_back({id, participant->name, participant->pid});
        }
    }
    return result;
}

const std::string& Hub::hostName() const noexcept
{
    return options_.hostName;
}

ParticipantId Hub::freeIdLocked() const
{
    // Reuse ids, but not ones whose channel still exists (a client that is just leaving).
    for (ParticipantId id = 1;; ++id) {
        const bool inFinished = std::ranges::any_of(
            finished_, [id](const auto& participant) { return participant->id == id; });
        if (!participants_.contains(id) && !inFinished) {
            return id;
        }
    }
}

std::optional<ParticipantId> Hub::acceptClient(pid_t pid)
{
    const std::lock_guard lock(mutex_);
    if (stopping_) {
        return std::nullopt;
    }
    const ParticipantId id = freeIdLocked();
    try {
        transport::Conn conn({getpid(), id}, true);
        auto participant = std::make_unique<Participant>(id, pid, std::move(conn));
        participant->lastActivityMs = proto::nowMs();
        Participant& ref = *participant;
        ref.reader =
            std::jthread([this, &ref](const std::stop_token& stop) { readLoop(ref, stop); });
        participants_.emplace(id, std::move(participant));
    } catch (const std::system_error& e) {
        log::error(std::format("cannot create a channel for pid {}: {}", pid, e.what()));
        return std::nullopt;
    }
    log::info(std::format("pid {} knocked, channel {} created", pid, id));
    return id;
}

void Hub::readLoop(Participant& participant, const std::stop_token& stop)
{
    const auto helloDeadline = std::chrono::steady_clock::now() + proto::kSyncTimeout;
    bool hello = false;
    std::string reason = "вышел(а) из чата";
    std::vector<std::byte> bytes;

    while (!stop.stop_requested()) {
        transport::ReadResult result{};
        try {
            result = participant.conn.read(bytes, kPollInterval);
        } catch (const std::system_error& e) {
            log::error(std::format("channel {}: {}", participant.id, e.what()));
            reason = "ошибка канала";
            break;
        }

        if (result == transport::ReadResult::Timeout) {
            if (!hello && std::chrono::steady_clock::now() > helloDeadline) {
                reason = "не представился(-ась) за 5 секунд";
                break;
            }
            continue;
        }
        if (result == transport::ReadResult::Closed) {
            if (participant.kicked) {
                reason = "отключён(а): молчал(а) больше минуты";
            }
            break;
        }

        auto message = proto::decode(bytes);
        if (!message) {
            log::warning(std::format("channel {}: malformed message dropped", participant.id));
            continue;
        }
        if (message->type == MessageType::Hello) {
            hello = true;
            handleHello(participant, *message);
        } else if (message->type == MessageType::Chat && hello) {
            handleChat(participant, std::move(*message));
        }
    }

    if (!stop.stop_requested()) {
        removeParticipant(participant.id, std::move(reason));
    }
}

void Hub::handleHello(Participant& participant, const Message& hello)
{
    const std::lock_guard lock(mutex_);
    if (participant.joined) {
        return;
    }
    participant.name = cleanName(hello.text, participant.id);
    participant.joined = true;
    participant.lastActivityMs = proto::nowMs();

    // The newcomer learns who is here; everyone else learns about the newcomer.
    participant.send(event(MessageType::Joined, proto::kHostId, options_.hostName));
    for (const auto& [id, other] : participants_) {
        if (id == participant.id || !other->joined) {
            continue;
        }
        participant.send(event(MessageType::Joined, id, other->name));
        other->send(event(MessageType::Joined, participant.id, participant.name));
    }
    log::info(std::format("participant {} '{}' (pid {}) joined", participant.id, participant.name,
                          participant.pid));
    listener_.onJoined(participant.id, participant.name, participant.pid);
}

void Hub::handleChat(Participant& participant, Message message)
{
    participant.lastActivityMs = proto::nowMs();
    if (message.text.empty()) {
        return;
    }
    message.from = participant.id;          // the client cannot pretend to be someone else
    message.forwardedFrom = proto::kNobody; // only the host forwards
    route(message);
}

void Hub::route(const Message& message)
{
    const std::lock_guard lock(mutex_);
    if (stopping_) {
        return;
    }

    if (message.to == proto::kBroadcast) {
        for (const auto& [id, participant] : participants_) {
            if (participant->joined) {
                participant->send(message);
            }
        }
        listener_.onMessage(message);
        return;
    }

    // A private message is shown only to its sender and its addressee.
    if (message.to != proto::kHostId) {
        const auto addressee = participants_.find(message.to);
        if (addressee == participants_.end() || !addressee->second->joined) {
            log::warning(std::format("message from {} to unknown participant {} dropped",
                                     message.from, message.to));
            return;
        }
        addressee->second->send(message);
    }
    if (message.from != proto::kHostId && message.from != message.to) {
        if (const auto sender = participants_.find(message.from); sender != participants_.end()) {
            sender->second->send(message); // the sender's copy, in the same order as everyone's
        }
    }
    if (message.to == proto::kHostId || message.from == proto::kHostId) {
        listener_.onMessage(message);
    }
}

void Hub::removeParticipant(ParticipantId id, std::string reason)
{
    const std::lock_guard lock(mutex_);
    if (stopping_) {
        return;
    }
    const auto it = participants_.find(id);
    if (it == participants_.end()) {
        return;
    }
    std::unique_ptr<Participant> participant = std::move(it->second);
    participants_.erase(it);

    if (participant->joined) {
        for (const auto& [otherId, other] : participants_) {
            if (other->joined) {
                other->send(event(MessageType::Left, id, reason));
            }
        }
        listener_.onLeft(id, participant->name, reason);
    }
    log::info(std::format("participant {} (pid {}) left: {}", id, participant->pid, reason));
    // This runs on the participant's own reader thread, which cannot join itself:
    // the watchdog destroys it a moment later.
    finished_.push_back(std::move(participant));
}

void Hub::watchdogLoop(const std::stop_token& stop)
{
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(kPollInterval);

        std::vector<std::unique_ptr<Participant>> finished;
        {
            const std::lock_guard lock(mutex_);
            finished.swap(finished_);

            const auto now = proto::nowMs();
            for (const auto& [id, participant] : participants_) {
                const auto silentMs = now - participant->lastActivityMs.load();
                if (participant->joined && !participant->kicked &&
                    silentMs > options_.inactivityLimit.count()) {
                    participant->kicked = true;
                    log::info(
                        std::format("participant {} silent for {} ms, sending SIGKILL to pid {}",
                                    id, silentMs, participant->pid));
                    if (::kill(participant->pid, SIGKILL) != 0) {
                        log::warning(std::format("kill({}): {}", participant->pid,
                                                 std::system_category().message(errno)));
                    }
                }
            }
        }
        finished.clear(); // joins the reader threads of participants that have left
    }
}

} // namespace chat::host
