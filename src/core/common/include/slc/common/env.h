#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace slc::env {

// Names from doc appendix B.
inline constexpr std::string_view kSteamRootVar = "SLC_STEAM_ROOT";
inline constexpr std::string_view kConfigPathVar = "SLC_CONFIG_PATH";
inline constexpr std::string_view kLogLevelVar = "SLC_LOG_LEVEL";
inline constexpr std::string_view kSgdbKeyVar = "SLC_SGDB_KEY";
inline constexpr std::string_view kTestIdleTimeoutVar = "SLC_TEST_IDLE_TIMEOUT_MS";
inline constexpr std::string_view kTestDisableInputVar = "SLC_TEST_DISABLE_INPUT_CHECK";

[[nodiscard]] std::optional<std::string> get(std::string_view name);
[[nodiscard]] std::optional<std::filesystem::path> getPath(std::string_view name);
[[nodiscard]] bool isTruthy(std::string_view name);

} // namespace slc::env
