#pragma once

#include <expected>
#include <source_location>
#include <string>
#include <string_view>

namespace slc {

enum class ErrorCode {
    NotFound,
    PermissionDenied,
    Malformed,
    IoError,
    Timeout,
    Unsupported,
};

struct Error {
    ErrorCode code;
    std::string message;
    std::source_location where;

    [[nodiscard]] static Error make(
        ErrorCode code,
        std::string message,
        const std::source_location& where = std::source_location::current())
    {
        return Error{code, std::move(message), where};
    }
};

template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

} // namespace slc
