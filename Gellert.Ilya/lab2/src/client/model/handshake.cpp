#include "handshake.hpp"

#include <chrono>
#include <csignal>
#include <format>
#include <stdexcept>
#include <system_error>

#include "posix/log.hpp"
#include "posix/signals.hpp"
#include "proto/constants.hpp"

namespace chat::client {

std::uint32_t knock(pid_t hostPid, int attempts)
{
    if (hostPid <= 0 || ::kill(hostPid, 0) != 0) {
        throw std::runtime_error(std::format("Хост с PID {} не найден", hostPid));
    }

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        try {
            posix::sendSignal(hostPid, proto::kHandshakeSignal);
        } catch (const std::system_error& e) {
            throw std::runtime_error(
                std::format("Не удалось отправить сигнал хосту: {}", e.what()));
        }

        const auto deadline = std::chrono::steady_clock::now() + proto::kSyncTimeout;
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                break;
            }
            const auto answer = posix::waitSignal({proto::kHandshakeSignal}, left);
            if (!answer) {
                break;
            }
            if (answer->senderPid != hostPid) {
                continue; // not from our host
            }
            if (answer->value < 0) {
                throw std::runtime_error("Хост отказал в подключении");
            }
            return static_cast<std::uint32_t>(answer->value);
        }
        log::warning(std::format("host did not answer, attempt {} of {}", attempt, attempts));
    }
    throw std::runtime_error("Хост не ответил за 5 секунд");
}

} // namespace chat::client
