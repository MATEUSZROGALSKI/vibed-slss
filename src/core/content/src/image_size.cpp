#include "slc/content/image_size.h"

#include <array>
#include <cstring>
#include <fstream>

namespace slc::content {
namespace {

constexpr std::size_t kHeaderRead = 1u << 16; // 64 KiB covers any SOF segment

std::uint32_t be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24u) |
           (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8u) | p[1]);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> pngSize(
    const std::uint8_t* d, std::size_t n)
{
    constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 33 || std::memcmp(d, kSig, 8) != 0) {
        return std::nullopt;
    }
    // First chunk must be IHDR: len(4) type(4) width(4) height(4)
    if (std::memcmp(d + 12, "IHDR", 4) != 0) {
        return std::nullopt;
    }
    const std::uint32_t w = be32(d + 16);
    const std::uint32_t h = be32(d + 20);
    if (w == 0 || h == 0) {
        return std::nullopt;
    }
    return std::make_pair(w, h);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> jpegSize(
    const std::uint8_t* d, std::size_t n)
{
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) {
        return std::nullopt;
    }
    std::size_t pos = 2;
    while (pos + 3 < n) {
        // Skip fill bytes then take the marker.
        if (d[pos] != 0xFF) {
            ++pos;
            continue;
        }
        const std::uint8_t marker = d[pos + 1];
        pos += 2;
        // Standalone markers (SOI/EOI/RSTn/TEM) carry no length.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            continue;
        }
        if (pos + 2 > n) {
            return std::nullopt;
        }
        const std::uint16_t segLen = be16(d + pos);
        if (segLen < 2 || pos + segLen > n) {
            return std::nullopt; // segment extends past what we read
        }
        // SOF0-2/5-7/9-11/13-15 hold the frame header (DHT/DAC/JPG/DNL/etc
        // do not).
        const bool isSof =
            marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
            marker != 0xC8 && marker != 0xCC;
        if (isSof) {
            if (segLen < 7) {
                return std::nullopt;
            }
            const std::uint32_t h = be16(d + pos + 3);
            const std::uint32_t w = be16(d + pos + 5);
            if (w == 0 || h == 0) {
                return std::nullopt;
            }
            return std::make_pair(w, h);
        }
        pos += segLen;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::pair<std::uint32_t, std::uint32_t>> probeImageSize(
    const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kHeaderRead> buf{};
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    const std::size_t n = static_cast<std::size_t>(in.gcount());
    if (n < 32) {
        return std::nullopt;
    }
    if (auto r = pngSize(buf.data(), n)) {
        return r;
    }
    return jpegSize(buf.data(), n);
}

} // namespace slc::content
