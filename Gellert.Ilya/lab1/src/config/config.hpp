#pragma once

#include <filesystem>
#include <istream>
#include <vector>

namespace monitor {

/**
 * @brief Holds the configuration data for the directory monitor.
 */
struct Config {
    /// Absolute path to the configuration file itself (used to re-read it on SIGHUP).
    std::filesystem::path cfgAbsPath;

    /// Absolute, normalized paths of the directories to monitor (no duplicates).
    std::vector<std::filesystem::path> directories;
};

/**
 * @brief Responsible for parsing configuration files.
 *
 * Format: one `KEY=VALUE` pair per line, `#` starts a comment line, empty lines are ignored.
 * The only supported key is `DIR`; it may appear any number of times.
 * Relative `DIR` values are resolved against the directory that contains the config file,
 * so they keep working after the daemon has changed its working directory to `/`.
 */
class ConfigParser {
public:
    /**
     * @brief Parses the specified configuration file.
     *
     * @param configFilePath Path to the configuration file (absolute or relative to the CWD).
     * @return A Config object populated with the parsed settings.
     * @throws std::runtime_error If the file cannot be opened or contains invalid syntax.
     */
    static Config parse(const std::filesystem::path& configFilePath);

    /**
     * @brief Parses configuration text from a stream.
     *
     * @param input   Stream with the configuration text.
     * @param baseDir Absolute directory that relative paths are resolved against.
     * @return Absolute, normalized, de-duplicated list of directories.
     * @throws std::runtime_error On a syntax error or if no directories are listed.
     */
    static std::vector<std::filesystem::path>
    parseDirectories(std::istream& input, const std::filesystem::path& baseDir);
};

} // namespace monitor
