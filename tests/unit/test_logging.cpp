#include "slc/common/logging.h"

#include "env_guard.h"

#include <gtest/gtest.h>

#include <filesystem>

// initFileLogging is once-only by design — keep all assertions in one test.
TEST(Logging, InitFileLoggingWritesLogFile)
{
    const auto dir =
        std::filesystem::temp_directory_path() / "slc-test-logging";
    std::filesystem::remove_all(dir);

    slc::logging::initFileLogging(dir);
    {
        test::EnvGuard guard("SLC_LOG_LEVEL", "trace");
        slc::logging::applyLevelFromEnv();
    }
    SLC_LOG_INFO("coverage test line");
    slc::logging::shutdown();

    const auto logFile = dir / "slc.log";
    ASSERT_TRUE(std::filesystem::exists(logFile));
    EXPECT_GT(std::filesystem::file_size(logFile), 0u);

    // Second init must be a documented no-op, not a crash.
    slc::logging::initFileLogging(dir);
    slc::logging::shutdown();
}
