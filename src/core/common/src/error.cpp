#include "slc/common/error.h"

namespace slc {

std::string_view toString(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::NotFound:         return "not_found";
    case ErrorCode::PermissionDenied: return "permission_denied";
    case ErrorCode::Malformed:        return "malformed";
    case ErrorCode::IoError:          return "io_error";
    case ErrorCode::Timeout:          return "timeout";
    case ErrorCode::Unsupported:      return "unsupported";
    }
    return "unknown";
}

} // namespace slc
