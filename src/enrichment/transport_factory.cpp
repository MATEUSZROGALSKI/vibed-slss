#include "transport_factory.h"

#ifdef SLC_WITH_CPR
namespace slc::enrichment {
std::unique_ptr<content::IHttpTransport> makeCprTransport();
}
#endif

namespace slc::enrichment {

std::unique_ptr<content::IHttpTransport> makeHttpTransport()
{
#ifdef SLC_WITH_CPR
    return makeCprTransport();
#else
    return nullptr; // enrichment feature not compiled in
#endif
}

} // namespace slc::enrichment
