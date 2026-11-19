#pragma once

#include "slc/content/ihttp_transport.h"

#include <memory>

namespace slc::enrichment {

/**
 * Production HTTP transport. Returns a cpr-backed transport when the
 * enrichment feature is compiled in (SLC_WITH_CPR), nullptr otherwise —
 * callers must handle the null case (enrichment silently disabled).
 */
[[nodiscard]] std::unique_ptr<content::IHttpTransport> makeHttpTransport();

} // namespace slc::enrichment
