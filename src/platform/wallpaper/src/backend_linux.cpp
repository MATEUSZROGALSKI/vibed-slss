// §6.1.4 Linux wallpaper backend. No desktop-toolkit dev headers are
// available in all the environments this must build in, so integration goes
// through the standard per-desktop command-line tools (gsettings, qdbus,
// swaymsg, feh) via posix_spawn — the same calls a user would type.
#include "slc/wallpaper/backend.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace slc::wallpaper {
namespace {

// Runs argv[0] (searched on PATH), waits for exit, returns exit status.
Result<int> runTool(const std::vector<std::string>& argv)
{
    if (argv.empty()) {
        return std::unexpected(Error::make(ErrorCode::Malformed, "empty argv"));
    }
    std::vector<std::string> storage = argv;
    std::vector<char*> args;
    args.reserve(storage.size() + 1);
    for (auto& s : storage) {
        args.push_back(s.data());
    }
    args.push_back(nullptr);

    pid_t pid = 0;
    if (const int rc = posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ);
        rc != 0) {
        return std::unexpected(Error::make(
            rc == ENOENT ? ErrorCode::NotFound : ErrorCode::IoError,
            "posix_spawnp(" + argv[0] + "): " + std::strerror(rc)));
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return std::unexpected(
                Error::make(ErrorCode::IoError, "waitpid: " + std::string(std::strerror(errno))));
        }
    }
    if (!WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

bool toolSucceeds(const std::vector<std::string>& argv)
{
    const auto rc = runTool(argv);
    return rc.has_value() && *rc == 0;
}

// Runs argv like runTool but captures stdout; returns the output on exit 0.
Result<std::string> runToolCapture(const std::vector<std::string>& argv)
{
    if (argv.empty()) {
        return std::unexpected(Error::make(ErrorCode::Malformed, "empty argv"));
    }
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, "pipe: " + std::string(std::strerror(errno))));
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);

    std::vector<std::string> storage = argv;
    std::vector<char*> args;
    args.reserve(storage.size() + 1);
    for (auto& s : storage) {
        args.push_back(s.data());
    }
    args.push_back(nullptr);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return std::unexpected(Error::make(
            rc == ENOENT ? ErrorCode::NotFound : ErrorCode::IoError,
            "posix_spawnp(" + argv[0] + "): " + std::strerror(rc)));
    }
    std::string out;
    char buf[512];
    ssize_t nr = 0;
    while ((nr = read(fds[0], buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(nr));
    }
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return std::unexpected(
                Error::make(ErrorCode::IoError, "waitpid: " + std::string(std::strerror(errno))));
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, argv[0] + " exited non-zero"));
    }
    return out;
}

std::string envOf(const char* name)
{
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

// "GNOME:GNOME-Flashback" -> ["gnome", "gnome-flashback"]
std::vector<std::string> desktopNames()
{
    std::vector<std::string> out;
    std::string raw = envOf("XDG_CURRENT_DESKTOP");
    if (raw.empty()) {
        raw = envOf("XDG_SESSION_DESKTOP");
    }
    std::string cur;
    for (const char c : raw) {
        if (c == ':' || c == ';') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

bool anyOf(const std::vector<std::string>& haystack, std::initializer_list<const char*> needles)
{
    for (const auto& h : haystack) {
        for (const char* n : needles) {
            if (h == n) {
                return true;
            }
        }
    }
    return false;
}

// gsettings 'get' prints 'file:///x' with single quotes; KDE writes plain
// file:///x. Returns the decoded local path, empty on anything else.
std::filesystem::path uriToPath(std::string uri)
{
    while (!uri.empty() &&
           (uri.back() == '\n' || uri.back() == '\r' || uri.back() == ' ')) {
        uri.pop_back();
    }
    if (uri.size() >= 2 && uri.front() == '\'' && uri.back() == '\'') {
        uri = uri.substr(1, uri.size() - 2);
    }
    if (uri.rfind("file://", 0) == 0) {
        uri.erase(0, 7);
    } else if (uri.front() != '/') {
        return {};
    }
    std::string out;
    out.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(uri[i + 1]);
            const int lo = hex(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += uri[i];
    }
    return std::filesystem::path(out);
}

std::string fileUri(const std::filesystem::path& p)
{
    // Percent-encode the characters gsettings URI parsing is picky about.
    static constexpr char kSafe[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/";
    std::string uri = "file://";
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char c : p.generic_string()) {
        if (std::strchr(kSafe, static_cast<char>(c)) != nullptr) {
            uri += static_cast<char>(c);
        } else {
            uri += '%';
            uri += kHex[c >> 4U];
            uri += kHex[c & 0x0FU];
        }
    }
    return uri;
}

class LinuxWallpaperBackend final : public IWallpaperBackend {
  public:
    Result<void> setWallpaper(const std::filesystem::path& image) override
    {
        if (!std::filesystem::exists(image)) {
            return std::unexpected(
                Error::make(ErrorCode::NotFound, "image not found: " + image.string()));
        }
        const std::string uri = fileUri(image);
        const auto desktops = desktopNames();

        // GNOME and derivatives that share org.gnome.desktop.background.
        if (anyOf(desktops, {"gnome", "ubuntu", "budgie", "unity", "pantheon"})) {
            if (!toolSucceeds({"gsettings", "set", "org.gnome.desktop.background", "picture-uri",
                               uri})) {
                return std::unexpected(Error::make(
                    ErrorCode::IoError, "gsettings failed to set picture-uri"));
            }
            // GNOME 42+ tracks a separate dark-mode image; keep them in sync.
            (void)toolSucceeds({"gsettings", "set", "org.gnome.desktop.background",
                                "picture-uri-dark", uri});
            return {};
        }
        if (anyOf(desktops, {"x-cinnamon", "cinnamon"})) {
            if (!toolSucceeds({"gsettings", "set", "org.cinnamon.desktop.background",
                               "picture-uri", uri})) {
                return std::unexpected(
                    Error::make(ErrorCode::IoError, "gsettings (cinnamon) failed"));
            }
            return {};
        }
        if (anyOf(desktops, {"kde", "plasma"})) {
            // Plasma stores wallpaper per-desktop containment; this JS loop is
            // the documented way to update all of them at once.
            const std::string script =
                "var ds=desktops();for(var i=0;i<ds.length;i++){var d=ds[i];"
                "d.wallpaperPlugin='org.kde.image';d.currentConfigGroup=['Wallpaper',"
                "'org.kde.image','General'];d.writeConfig('Image','" +
                uri + "');}";
            for (const char* tool : {"qdbus6", "qdbus", "qdbus-qt5"}) {
                if (toolSucceeds({tool, "org.kde.plasmashell", "/PlasmaShell",
                                  "org.kde.PlasmaShell.evaluateScript", script})) {
                    return {};
                }
            }
            return std::unexpected(
                Error::make(ErrorCode::IoError, "qdbus evaluateScript failed"));
        }
        if (anyOf(desktops, {"sway"})) {
            if (toolSucceeds({"swaymsg", "output", "*", "bg", image.string(), "fill"})) {
                return {};
            }
            return std::unexpected(Error::make(ErrorCode::IoError, "swaymsg failed"));
        }

        // Generic X11 fallback: feh then xwallpaper, whichever is installed.
        if (toolSucceeds({"feh", "--bg-fill", image.string()}) ||
            toolSucceeds({"xwallpaper", "--zoom", image.string()})) {
            return {};
        }
        return std::unexpected(Error::make(
            ErrorCode::Unsupported,
            "no supported wallpaper mechanism (desktop='" + envOf("XDG_CURRENT_DESKTOP") +
                "'; tried gsettings/qdbus/swaymsg/feh/xwallpaper)"));
    }

    [[nodiscard]] Result<std::filesystem::path> currentWallpaper()
        const override
    {
        const auto desktops = desktopNames();
        if (anyOf(desktops,
                  {"gnome", "ubuntu", "budgie", "unity", "pantheon"})) {
            if (const auto out = runToolCapture(
                    {"gsettings", "get", "org.gnome.desktop.background",
                     "picture-uri"})) {
                if (auto p = uriToPath(*out); !p.empty()) {
                    return p;
                }
            }
        } else if (anyOf(desktops, {"x-cinnamon", "cinnamon"})) {
            if (const auto out = runToolCapture(
                    {"gsettings", "get",
                     "org.cinnamon.desktop.background", "picture-uri"})) {
                if (auto p = uriToPath(*out); !p.empty()) {
                    return p;
                }
            }
        } else if (anyOf(desktops, {"kde", "plasma"})) {
            // Plasma persists Image= in the containment config.
            const char* home = std::getenv("HOME");
            if (home != nullptr) {
                const std::filesystem::path cfg =
                    std::filesystem::path(home) /
                    ".config/plasma-org.kde.plasma.desktop-appletsrc";
                std::ifstream in(cfg);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.rfind("Image=", 0) == 0) {
                        if (auto p = uriToPath(line.substr(6)); !p.empty()) {
                            return p;
                        }
                    }
                }
            }
        } else {
            // feh/xwallpaper path: ~/.fehbg holds the last feh --bg command.
            const char* home = std::getenv("HOME");
            if (home != nullptr) {
                std::ifstream in(std::filesystem::path(home) / ".fehbg");
                std::string line;
                while (std::getline(in, line)) {
                    const auto pos = line.rfind("--bg-");
                    if (pos == std::string::npos) {
                        continue;
                    }
                    const auto q1 = line.find('\'', pos);
                    const auto q2 =
                        q1 == std::string::npos
                            ? std::string::npos
                            : line.find('\'', q1 + 1);
                    if (q2 != std::string::npos) {
                        return std::filesystem::path(
                            line.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
            }
        }
        return std::unexpected(Error::make(
            ErrorCode::Unsupported,
            "currentWallpaper unavailable for desktop '" +
                envOf("XDG_CURRENT_DESKTOP") + "'"));
    }
};

} // namespace

std::unique_ptr<IWallpaperBackend> makeWallpaperBackend()
{
    return std::make_unique<LinuxWallpaperBackend>();
}

} // namespace slc::wallpaper
