// libsecret backend via the secret-tool CLI (§6.2): avoiding a link-time
// libsecret dep keeps portable-core builds free of glib. secret-tool is
// part of every desktop distro's libsecret package; when it's missing
// (headless) loadCredential returns nullopt and the env-var path stays
// the fallback.

#include "slc/platform/credential.h"

#include <cstdio>
#include <string>
#include <vector>

namespace slc::platform {
namespace {

constexpr const char* kLabel = "SteamLibraryScreensaver";
constexpr const char* kService = "io.github.slc";

// Runs secret-tool with args; `input` (may be empty) is fed to stdin.
// Returns exit code + captured stdout.
std::pair<int, std::string> runSecretTool(
    const std::vector<std::string>& args, const std::string& input)
{
    std::string cmd = "secret-tool";
    for (const auto& a : args) {
        cmd += " '";
        for (const char c : a) {
            cmd += c == '\'' ? std::string("'\\''") : std::string(1, c);
        }
        cmd += '\'';
    }
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), input.empty() ? "r" : "w");
    if (!pipe) {
        return {-1, {}};
    }
    if (!input.empty()) {
        std::fwrite(input.data(), 1, input.size(), pipe);
    }
    std::string out;
    if (!input.empty() || true) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe)) {
            out += buf;
        }
    }
    return {pclose(pipe), out};
}

} // namespace

bool storeCredential(const std::string& key, const std::string& value)
{
    const auto [rc, out] = runSecretTool(
        {"store", "--label", std::string(kLabel) + " " + key, "service",
         kService, "key", key},
        value);
    return rc == 0;
}

std::optional<std::string> loadCredential(const std::string& key)
{
    const auto [rc, out] =
        runSecretTool({"lookup", "service", kService, "key", key}, {});
    if (rc != 0 || out.empty()) {
        return std::nullopt;
    }
    std::string value = out;
    while (!value.empty() && value.back() == '\n') {
        value.pop_back();
    }
    return value;
}

bool deleteCredential(const std::string& key)
{
    const auto [rc, out] =
        runSecretTool({"clear", "service", kService, "key", key}, {});
    return rc == 0;
}

} // namespace slc::platform
