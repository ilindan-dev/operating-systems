#include "handshake_listener.hpp"

#include <chrono>
#include <format>
#include <system_error>
#include <utility>

#include "posix/log.hpp"
#include "posix/signals.hpp"
#include "proto/constants.hpp"

namespace chat::host {

namespace {

/// How often the thread wakes up to check whether it should stop.
constexpr std::chrono::milliseconds kPollInterval{200};
constexpr int kRefused = -1;

} // namespace

HandshakeListener::HandshakeListener(OnKnock onKnock) : onKnock_(std::move(onKnock)) {}

HandshakeListener::~HandshakeListener()
{
    stop();
}

void HandshakeListener::start()
{
    thread_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

void HandshakeListener::stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void HandshakeListener::run(const std::stop_token& stop) const
{
    while (!stop.stop_requested()) {
        std::optional<posix::SignalInfo> knock;
        try {
            knock = posix::waitSignal({proto::kHandshakeSignal}, kPollInterval);
        } catch (const std::system_error& e) {
            log::error(std::format("waiting for clients failed: {}", e.what()));
            return;
        }
        if (!knock) {
            continue;
        }

        const pid_t client = knock->senderPid;
        const std::optional<std::uint32_t> id = onKnock_(client);
        try {
            posix::sendSignal(client, proto::kHandshakeSignal,
                              id ? static_cast<int>(*id) : kRefused);
        } catch (const std::system_error& e) {
            log::warning(std::format("cannot answer client {}: {}", client, e.what()));
        }
    }
}

} // namespace chat::host
