#include "config.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace monitor {

namespace {

constexpr char kDelimiter = '=';
constexpr char kCommentChar = '#';
constexpr std::string_view kDirKey = "DIR";
constexpr std::string_view kWhitespace = " \t\n\r\f\v";

std::string_view trim(std::string_view str)
{
    const auto start = str.find_first_not_of(kWhitespace);
    if (start == std::string_view::npos) {
        return {};
    }
    const auto end = str.find_last_not_of(kWhitespace);
    return str.substr(start, end - start + 1);
}

std::runtime_error syntaxError(std::size_t lineNo, const std::string& what)
{
    return std::runtime_error("Config syntax error at line " + std::to_string(lineNo) + ": " +
                              what);
}

/// "/a/./b/" -> "/a/b", relative paths are resolved against baseDir. Does not touch the disk.
fs::path normalizeDir(const fs::path& value, const fs::path& baseDir)
{
    fs::path dir = value.is_relative() ? baseDir / value : value;
    dir = dir.lexically_normal();
    if (!dir.has_filename() && dir != dir.root_path()) {
        dir = dir.parent_path();
    }
    return dir;
}

} // namespace

std::vector<fs::path> ConfigParser::parseDirectories(std::istream& input, const fs::path& baseDir)
{
    std::vector<fs::path> directories;
    std::string line;
    std::size_t lineNo = 0;

    while (std::getline(input, line)) {
        ++lineNo;
        const std::string_view stripped = trim(line);

        if (stripped.empty() || stripped.front() == kCommentChar) {
            continue;
        }

        const auto delimiterPos = stripped.find(kDelimiter);
        if (delimiterPos == std::string_view::npos) {
            throw syntaxError(lineNo, "expected KEY=VALUE");
        }

        const std::string_view key = trim(stripped.substr(0, delimiterPos));
        const std::string_view value = trim(stripped.substr(delimiterPos + 1));

        if (key != kDirKey) {
            throw syntaxError(lineNo, "unknown key '" + std::string(key) + "'");
        }
        if (value.empty()) {
            throw syntaxError(lineNo, "empty DIR value");
        }

        fs::path dir = normalizeDir(fs::path(std::string(value)), baseDir);
        if (std::ranges::find(directories, dir) == directories.end()) {
            directories.push_back(std::move(dir));
        }
    }

    if (input.bad()) {
        throw std::runtime_error("I/O error while reading configuration");
    }
    if (directories.empty()) {
        throw std::runtime_error("No directories (DIR=...) found in config file");
    }
    return directories;
}

Config ConfigParser::parse(const fs::path& configFilePath)
{
    Config config;
    config.cfgAbsPath = fs::absolute(configFilePath).lexically_normal();

    std::ifstream configFile(config.cfgAbsPath);
    if (!configFile.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " +
                                 config.cfgAbsPath.string());
    }

    config.directories = parseDirectories(configFile, config.cfgAbsPath.parent_path());
    return config;
}

} // namespace monitor
