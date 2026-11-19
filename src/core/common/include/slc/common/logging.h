#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <string_view>

namespace slc::logging {

/**
 * Initialises the rotating file logger described in doc section 6.4.
 * Safe to call once; subsequent calls are no-ops.
 * \param logDir Directory that will hold slc.log (5 x 2 MB rotation).
 * \threadsafety Not thread-safe; call from main() before threads start.
 */
void initFileLogging(const std::filesystem::path& logDir);

/** Applies SLC_LOG_LEVEL to the default logger. */
void applyLevelFromEnv();

void shutdown();

} // namespace slc::logging

#define SLC_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define SLC_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define SLC_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define SLC_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define SLC_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define SLC_LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

// Fatal invariant check: logs then terminates. Never used for input validation.
#define SLC_ASSERT(cond, ...)                                                                    \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            SPDLOG_CRITICAL("assertion failed: {} {}", #cond, fmt::format(__VA_ARGS__));           \
            std::terminate();                                                                    \
        }                                                                                        \
    } while (0)
