#include "slc/common/env.h"

#include <cstdlib>

namespace slc::env {

std::optional<std::string> get(std::string_view name)
{
    const std::string key(name);
#if defined(_MSC_VER)
    // MSVC marks getenv unsafe (C4996); _dupenv_s allocates, free it.
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, key.c_str()) != 0 || value == nullptr ||
        len == 0) {
        return std::nullopt;
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::optional<std::filesystem::path> getPath(std::string_view name)
{
    auto value = get(name);
    if (!value) {
        return std::nullopt;
    }
    return std::filesystem::path(*value);
}

bool isTruthy(std::string_view name)
{
    auto value = get(name);
    if (!value) {
        return false;
    }
    return *value == "1" || *value == "true" || *value == "yes" || *value == "on";
}

namespace {
bool g_allowTestHooks = false;
}

void setAllowTestHooks(bool enabled)
{
    g_allowTestHooks = enabled;
}

bool allowTestHooks()
{
    return g_allowTestHooks;
}

std::optional<std::string> getTestHook(std::string_view name)
{
    if (!g_allowTestHooks) {
        return std::nullopt;
    }
    return get(name);
}

bool testHookTruthy(std::string_view name)
{
    return g_allowTestHooks && isTruthy(name);
}

} // namespace slc::env
