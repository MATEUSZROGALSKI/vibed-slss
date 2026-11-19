#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace slc::content {

/// Reads an image's pixel dimensions from its header without decoding it.
/// Supports PNG (IHDR) and JPEG (SOFn segments) — the only formats
/// discovery accepts. Returns nullopt for anything else or on I/O failure.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
probeImageSize(const std::filesystem::path& path);

} // namespace slc::content
