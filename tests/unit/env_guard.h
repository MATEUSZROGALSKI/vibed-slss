#pragma once

// RAII env-var helper for unit tests: sets a variable on construction and
// restores the previous value (or unsets it) on destruction. Not in
// src/core — tests may use platform conditionals.

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace test {

inline void setEnv(std::string_view name, std::string_view value)
{
#if defined(_WIN32)
    _putenv_s(std::string(name).c_str(), std::string(value).c_str());
#else
    ::setenv(std::string(name).c_str(), std::string(value).c_str(), 1);
#endif
}

inline void unsetEnv(std::string_view name)
{
#if defined(_WIN32)
    _putenv_s(std::string(name).c_str(), "");
#else
    ::unsetenv(std::string(name).c_str());
#endif
}

class EnvGuard {
  public:
    EnvGuard(std::string_view name, std::string_view value)
        : m_name(name)
    {
        if (const char* prev = std::getenv(m_name.c_str())) {
            m_prev = prev;
        }
        setEnv(m_name, value);
    }

    ~EnvGuard()
    {
        if (m_prev) {
            setEnv(m_name, *m_prev);
        } else {
            unsetEnv(m_name);
        }
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

  private:
    std::string m_name;
    std::optional<std::string> m_prev;
};

} // namespace test
