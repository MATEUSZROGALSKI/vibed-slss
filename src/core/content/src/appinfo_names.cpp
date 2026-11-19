#include "slc/content/appinfo_names.h"

#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

// appinfo.vdf record/VDF layout (per xPaw/SteamAppInfo and the public
// ValvePython/node-binary-vdf implementations):
//
//   u32 magic              0x07564427/28/29 = v39/v40/v41
//   u32 universe           always 1
//   u64 stringTableOffset  v41 only; u32 count + cstrings live there
//   records until appid==0:
//     u32 appid
//     u32 size             bytes from *after* this field to the record end
//     u32 infoState / u32 lastUpdated / u64 picsToken
//     u8[20] sha1Text / u32 changeNumber / u8[20] sha1Binary (v40+)
//     binary VDF           appinfo{ appid, common{ name, ... }, ... }
//
// Binary VDF entries are type(1) + key + value; key is a cstring for v39/40
// and a u32 string-table index for v41. Types mirror classic KeyValues:
// 0=section, 1=cstring, 2=int32, 3=float32, 4=pointer, 5=wstring,
// 6=color, 7=uint64, 8/10=end.

namespace slc::content {
namespace {

constexpr std::uint32_t kMagicV39 = 0x07564427;
constexpr std::uint32_t kMagicV40 = 0x07564428;
constexpr std::uint32_t kMagicV41 = 0x07564429;
constexpr std::size_t kMaxFileSize = 256u * 1024u * 1024u;
constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxTableStrings = 1u << 20;

struct Reader {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* end = nullptr;

    [[nodiscard]] std::size_t remaining() const
    {
        return static_cast<std::size_t>(end - p);
    }

    template <typename T> bool read(T& out)
    {
        if (remaining() < sizeof(T)) {
            return false;
        }
        std::memcpy(&out, p, sizeof(T));
        p += sizeof(T);
        return true;
    }

    bool skip(std::size_t n)
    {
        if (remaining() < n) {
            return false;
        }
        p += n;
        return true;
    }

    std::optional<std::string_view> cstring()
    {
        const void* nul = std::memchr(p, 0, remaining());
        if (nul == nullptr) {
            return std::nullopt;
        }
        const auto* term = static_cast<const std::uint8_t*>(nul);
        std::string_view out(reinterpret_cast<const char*>(p),
                             static_cast<std::size_t>(term - p));
        p = term + 1;
        return out;
    }
};

/// Binary-VDF scanner that understands both key encodings and only extracts
/// the "common"->"name" string per record.
class RecordScanner {
  public:
    RecordScanner(const std::uint8_t* p, const std::uint8_t* end,
                  const std::vector<std::string>* table)
        : r_{p, end}, table_(table)
    {
    }

    /// Walks the record body; returns the common section's name/type fields.
    std::optional<AppInfoEntry> findCommon()
    {
        scan(/*inCommon=*/false, 0);
        if (entry_.name.empty()) {
            return std::nullopt;
        }
        return std::move(entry_);
    }

  private:
    bool readKey(std::string_view& out)
    {
        if (table_ != nullptr) {
            std::uint32_t idx = 0;
            if (!r_.read(idx) || idx >= table_->size()) {
                return false;
            }
            out = (*table_)[idx];
            return true;
        }
        const auto s = r_.cstring();
        if (!s) {
            return false;
        }
        out = *s;
        return true;
    }

    bool skipValue(std::uint8_t type, int depth)
    {
        switch (type) {
        case 0x00:
            return skipSection(depth + 1);
        case 0x01:
            return r_.cstring().has_value();
        case 0x02: // int32
        case 0x03: // float32
        case 0x04: // pointer
        case 0x06: // color
            return r_.skip(4);
        case 0x05: { // wstring: u16 char count + UTF-16 data (best effort)
            std::uint16_t n = 0;
            return r_.read(n) && r_.skip(static_cast<std::size_t>(n) * 2u);
        }
        case 0x07: // uint64
        case 0x09: // int64
            return r_.skip(8);
        default:
            return false;
        }
    }

    /// Consumes entries until this section's END marker.
    bool skipSection(int depth)
    {
        if (depth > kMaxDepth) {
            return false;
        }
        while (r_.p < r_.end) {
            const std::uint8_t type = *r_.p++;
            if (type == 0x08 || type == 0x0A) {
                return true;
            }
            std::string_view key;
            if (!readKey(key) || !skipValue(type, depth)) {
                return false;
            }
        }
        return false;
    }

    /// Consumes one section. Inside "common" it captures the name and type
    /// strings; returns true once both were seen (early exit is safe — the
    /// caller owns the reader and nothing after common is needed).
    bool scan(bool inCommon, int depth)
    {
        if (depth > kMaxDepth) {
            return false;
        }
        while (r_.p < r_.end) {
            const std::uint8_t type = *r_.p++;
            if (type == 0x08 || type == 0x0A) {
                return false;
            }
            std::string_view key;
            if (!readKey(key)) {
                return false;
            }
            if (type == 0x00) {
                if (inCommon) {
                    if (!skipSection(depth + 1)) {
                        return false;
                    }
                    continue;
                }
                // Every sibling section is descended — "common" is nested
                // inside "appinfo", not a top-level key.
                if (scan(key == "common", depth + 1) ||
                    !entry_.name.empty()) {
                    return true;
                }
                continue;
            }
            if (type == 0x01 && inCommon &&
                (key == "name" || key == "type")) {
                if (auto v = r_.cstring()) {
                    (key == "name" ? entry_.name : entry_.type) = *v;
                    if (!entry_.name.empty() && !entry_.type.empty()) {
                        return true;
                    }
                    continue;
                }
                return false;
            }
            if (!skipValue(type, depth)) {
                return false;
            }
        }
        return false;
    }

    AppInfoEntry entry_;
    Reader r_;
    const std::vector<std::string>* table_;
};

std::optional<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxFileSize) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> buf(size);
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size)) {
        return std::nullopt;
    }
    return buf;
}

std::vector<std::string> readStringTable(const std::uint8_t* base,
                                         std::uint64_t offset,
                                         std::size_t fileSize)
{
    std::vector<std::string> table;
    if (offset + 4 >= fileSize) {
        return table;
    }
    Reader r{base + offset, base + fileSize};
    std::uint32_t count = 0;
    if (!r.read(count) || count > kMaxTableStrings) {
        return table;
    }
    table.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto s = r.cstring();
        if (!s) {
            break; // truncated table: keep the strings already read
        }
        table.emplace_back(*s);
    }
    return table;
}

} // namespace

std::unordered_map<std::string, AppInfoEntry> readAppInfo(
    const std::filesystem::path& appinfoPath)
{
    std::unordered_map<std::string, AppInfoEntry> infos;
    const auto data = readFile(appinfoPath);
    if (!data || data->size() < 16) {
        return infos;
    }
    const std::uint8_t* base = data->data();
    const std::size_t size = data->size();

    Reader head{base, base + size};
    std::uint32_t magic = 0;
    if (!head.read(magic)) {
        return infos;
    }

    std::size_t recordStart = 0;
    std::size_t recordHeader = 0;
    std::size_t vdfLimit = size;
    std::vector<std::string> table;
    const std::vector<std::string>* tablePtr = nullptr;

    if (magic == kMagicV41) {
        std::uint64_t tableOffset = 0;
        if (!head.skip(4) || !head.read(tableOffset)) {
            return infos;
        }
        recordStart = 16;
        recordHeader = 68;
        table = readStringTable(base, tableOffset, size);
        tablePtr = &table;
        if (tableOffset < vdfLimit && tableOffset >= recordStart) {
            vdfLimit = static_cast<std::size_t>(tableOffset);
        }
    } else if (magic == kMagicV40) {
        recordStart = 8;
        recordHeader = 68;
    } else if (magic == kMagicV39) {
        recordStart = 8;
        recordHeader = 48; // no binary SHA
    } else {
        return infos;
    }

    // `size` counts bytes after the size field, so a record spans
    // [pos, pos + 8 + size) and its binary VDF starts at pos + recordHeader.
    const std::size_t minSize = recordHeader - 8;
    std::size_t pos = recordStart;
    while (pos + 8 <= vdfLimit) {
        std::uint32_t appId = 0;
        std::uint32_t recordSize = 0;
        std::memcpy(&appId, base + pos, 4);
        std::memcpy(&recordSize, base + pos + 4, 4);
        if (appId == 0) {
            break;
        }
        if (recordSize < minSize || pos + 8 + recordSize > vdfLimit) {
            break; // malformed record: stop rather than misalign the stream
        }
        const auto* vdfBegin = base + pos + recordHeader;
        const auto* vdfEnd = base + pos + 8 + recordSize;
        RecordScanner scanner{vdfBegin, vdfEnd, tablePtr};
        if (auto info = scanner.findCommon()) {
            infos.emplace(std::to_string(appId), std::move(*info));
        }
        pos += 8 + recordSize;
    }
    return infos;
}

} // namespace slc::content
