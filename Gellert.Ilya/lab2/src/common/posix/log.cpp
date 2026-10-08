#include "log.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>

namespace chat::log {

namespace {

std::mutex& mutex()
{
    static std::mutex instance;
    return instance;
}

std::string& role()
{
    static std::string instance = "chat";
    return instance;
}

void write(std::string_view level, std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm local{};
    localtime_r(&seconds, &local);

    const std::lock_guard lock(mutex());
    std::fprintf(stderr, "%s\n",
                 std::format("{:02}:{:02}:{:02}.{:03} [{} {}] {}: {}", local.tm_hour, local.tm_min,
                             local.tm_sec, millis, role(), getpid(), level, message)
                     .c_str());
}

} // namespace

void setRole(std::string newRole)
{
    const std::lock_guard lock(mutex());
    role() = std::move(newRole);
}

void info(const std::string_view message)
{
    write("INFO", message);
}

void warning(const std::string_view message)
{
    write("WARN", message);
}

void error(const std::string_view message)
{
    write("ERROR", message);
}

} // namespace chat::log
