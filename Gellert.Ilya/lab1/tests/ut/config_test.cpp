#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "common/test_utils.hpp"
#include "config/config.hpp"

namespace fs = std::filesystem;
using monitor::ConfigParser;
using Paths = std::vector<fs::path>;

namespace {

Paths parse(const std::string& text, const fs::path& baseDir = "/base")
{
    std::istringstream input(text);
    return ConfigParser::parseDirectories(input, baseDir);
}

std::string parseError(const std::string& text)
{
    return test::errorMessage([&] { parse(text); });
}

} // namespace

// ---------- Syntax ----------

TEST(ConfigParserTest, ParsesSingleDirectory)
{
    EXPECT_EQ(parse("DIR=/var/data\n"), Paths{"/var/data"});
}

TEST(ConfigParserTest, ParsesSeveralDirectoriesInOrder)
{
    EXPECT_EQ(parse("DIR=/b\nDIR=/a\nDIR=/c"), (Paths{"/b", "/a", "/c"}));
}

TEST(ConfigParserTest, IgnoresCommentsAndEmptyLines)
{
    EXPECT_EQ(parse("# comment\n\n   \n\t# indented comment\nDIR=/a\n"), Paths{"/a"});
}

TEST(ConfigParserTest, TrimsWhitespaceAroundKeyAndValue)
{
    EXPECT_EQ(parse("   DIR \t=  /with space  \t\n"), Paths{"/with space"});
}

TEST(ConfigParserTest, AcceptsWindowsLineEndings)
{
    EXPECT_EQ(parse("DIR=/a\r\nDIR=/b\r\n"), (Paths{"/a", "/b"}));
}

TEST(ConfigParserTest, ValueMayContainDelimiter)
{
    EXPECT_EQ(parse("DIR=/a=b"), Paths{"/a=b"});
}

// ---------- Path handling ----------

TEST(ConfigParserTest, ResolvesRelativePathsAgainstBaseDir)
{
    EXPECT_EQ(parse("DIR=data\nDIR=./x/../y\n", "/etc/monitor"),
              (Paths{"/etc/monitor/data", "/etc/monitor/y"}));
}

TEST(ConfigParserTest, NormalizesTrailingSlashesAndDots)
{
    EXPECT_EQ(parse("DIR=/a/b/\nDIR=/a/./c//\nDIR=/"), (Paths{"/a/b", "/a/c", "/"}));
}

TEST(ConfigParserTest, RemovesDuplicateDirectories)
{
    EXPECT_EQ(parse("DIR=/a\nDIR=/a/\nDIR=/b/../a\nDIR=a\n", "/"), Paths{"/a"});
}

// ---------- Errors ----------

TEST(ConfigParserTest, RejectsLineWithoutDelimiter)
{
    const std::string error = parseError("DIR=/a\njust some text\n");
    EXPECT_TRUE(test::contains(error, "line 2")) << error;
    EXPECT_TRUE(test::contains(error, "KEY=VALUE")) << error;
}

TEST(ConfigParserTest, RejectsUnknownKey)
{
    const std::string error = parseError("DIRS=/a\n");
    EXPECT_TRUE(test::contains(error, "unknown key 'DIRS'")) << error;
}

TEST(ConfigParserTest, KeyIsCaseSensitive)
{
    EXPECT_THROW(parse("dir=/a\n"), std::runtime_error);
}

TEST(ConfigParserTest, RejectsEmptyValue)
{
    const std::string error = parseError("DIR=   \n");
    EXPECT_TRUE(test::contains(error, "empty DIR value")) << error;
}

TEST(ConfigParserTest, RejectsConfigWithoutDirectories)
{
    EXPECT_THROW(parse(""), std::runtime_error);
    EXPECT_THROW(parse("# only a comment\n\n"), std::runtime_error);
}

// ---------- Reading from disk ----------

namespace {

class ConfigFileTest : public ::testing::Test {
protected:
    test::TempDir dir;
};

} // namespace

TEST_F(ConfigFileTest, ThrowsIfFileDoesNotExist)
{
    const std::string error =
        test::errorMessage([&] { ConfigParser::parse(dir / "missing.conf"); });
    EXPECT_TRUE(test::contains(error, "Failed to open configuration file")) << error;
}

TEST_F(ConfigFileTest, StoresAbsolutePathOfConfig)
{
    test::writeFile(dir / "monitor.conf", "DIR=/a\n");
    const test::CurrentDirGuard cwd(dir.path());

    const auto config = ConfigParser::parse("monitor.conf");

    EXPECT_TRUE(config.cfgAbsPath.is_absolute());
    EXPECT_EQ(config.cfgAbsPath, dir / "monitor.conf");
}

TEST_F(ConfigFileTest, ResolvesRelativeDirectoryAgainstConfigLocationNotCwd)
{
    fs::create_directories(dir / "conf");
    fs::create_directories(dir / "elsewhere");
    test::writeFile(dir / "conf" / "monitor.conf", "DIR=watched\n");
    const test::CurrentDirGuard cwd(dir / "elsewhere");

    const auto config = ConfigParser::parse("../conf/monitor.conf");

    EXPECT_EQ(config.directories, Paths{dir / "conf" / "watched"});
}

TEST_F(ConfigFileTest, RereadingPicksUpChanges)
{
    const fs::path file = dir / "monitor.conf";
    test::writeFile(file, "DIR=/first\n");
    const auto before = ConfigParser::parse(file);

    test::writeFile(file, "DIR=/second\nDIR=/third\n");
    const auto after = ConfigParser::parse(before.cfgAbsPath);

    EXPECT_EQ(before.directories, Paths{"/first"});
    EXPECT_EQ(after.directories, (Paths{"/second", "/third"}));
}
