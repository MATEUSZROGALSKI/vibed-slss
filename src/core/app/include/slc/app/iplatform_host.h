#pragma once

#include "slc/platform/paths.h"

#include <vector>

namespace slc::app {

/// Doc section 5.4 — contract every OS host fulfils.
class IPlatformHost {
  public:
    virtual ~IPlatformHost() = default;
    [[nodiscard]] virtual bool isPreviewMode() const noexcept = 0;
    virtual void requestExit(int code) = 0;
    [[nodiscard]] virtual std::vector<platform::DisplayInfo> displays() const = 0;
};

} // namespace slc::app
