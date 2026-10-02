#include <gtest/gtest.h>
#include <syslog.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // Keep the code under test from writing to the system journal.
    setlogmask(LOG_UPTO(LOG_EMERG));

    return RUN_ALL_TESTS();
}
