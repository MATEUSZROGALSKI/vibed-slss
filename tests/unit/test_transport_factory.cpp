#include "transport_factory.h"

#include <gtest/gtest.h>

// Without SLC_WITH_CPR the factory must return nullptr — callers treat it
// as "enrichment silently disabled" (doc §6.2).
TEST(TransportFactory, ReturnsTransportOrNull)
{
    auto transport = slc::enrichment::makeHttpTransport();
#if defined(SLC_WITH_CPR)
    EXPECT_NE(transport, nullptr);
#else
    EXPECT_EQ(transport, nullptr);
#endif
}
