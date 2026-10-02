#include <syslog.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>

#include "config/config.hpp"
#include "daemon/daemon.hpp"

namespace {

constexpr const char* kDefaultConfigFile = "disk-monitor.conf";

} // namespace

int main(int argc, char* argv[])
{
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [config-file]\n";
        return EXIT_FAILURE;
    }

    openlog("disk-monitor", LOG_PID | LOG_CONS, LOG_LOCAL0);

    const std::filesystem::path configFilePath = argc > 1 ? argv[1] : kDefaultConfigFile;

    try {
        // Before daemonization, so that errors are still visible in the terminal.
        const monitor::Config config = monitor::ConfigParser::parse(configFilePath);

        auto& daemon = monitor::Daemon::getInstance();
        daemon.init(config);
        daemon.run();
    } catch (const std::exception& e) {
        std::cerr << "disk_monitor: " << e.what() << '\n';
        syslog(LOG_ERR, "Fatal error: %s", e.what());
        closelog();
        return EXIT_FAILURE;
    }

    closelog();
    return EXIT_SUCCESS;
}
