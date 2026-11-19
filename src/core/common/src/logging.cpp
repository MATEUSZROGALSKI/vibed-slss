#include "slc/common/logging.h"

#include "slc/common/env.h"

#include <spdlog/sinks/rotating_file_sink.h>

#include <chrono>
#include <mutex>

namespace slc::logging {
namespace {

constexpr std::size_t kMaxFileSizeBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxFiles = 5;
constexpr std::string_view kPattern = "%Y-%m-%dT%H:%M:%S.%e%z [%l] [%t] %n: %v";
constexpr std::string_view kDefaultLevel = "info";

std::once_flag g_once;

spdlog::level::level_enum levelFromString(std::string_view level)
{
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn")  return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    return spdlog::level::info;
}

} // namespace

void applyLevelFromEnv()
{
    const auto level = env::get(env::kLogLevelVar).value_or(std::string(kDefaultLevel));
    spdlog::set_level(levelFromString(level));
}

void initFileLogging(const std::filesystem::path& logDir)
{
    std::call_once(g_once, [&logDir] {
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        const auto logFile = logDir / "slc.log";
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile.string(), kMaxFileSizeBytes, kMaxFiles);
        auto logger = std::make_shared<spdlog::logger>("slc", std::move(sink));
        logger->set_pattern(std::string(kPattern));
        spdlog::set_default_logger(std::move(logger));
        applyLevelFromEnv();
        spdlog::flush_on(spdlog::level::warn);
        // Info-level lines would otherwise sit in the stdio buffer until the
        // first warning; flush periodically so logs are usable live.
        spdlog::flush_every(std::chrono::seconds(2));
    });
}

void shutdown()
{
    spdlog::shutdown();
}

} // namespace slc::logging
