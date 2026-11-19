#include "slc/platform/credential.h"

#include <gtest/gtest.h>

// The OS credential store may be unavailable (CI containers, locked
// keyrings). A successful store must round-trip; a failed store must
// still leave load/delete safe to call.
TEST(Credential, RoundTripOrGracefulFailure)
{
    constexpr const char* kKey = "slc.coverage.test";
    constexpr const char* kValue = "test-value-12345";

    slc::platform::deleteCredential(kKey); // ensure clean slate
    EXPECT_FALSE(slc::platform::loadCredential(kKey).has_value());

    if (slc::platform::storeCredential(kKey, kValue)) {
        auto loaded = slc::platform::loadCredential(kKey);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(*loaded, kValue);
        EXPECT_TRUE(slc::platform::deleteCredential(kKey));
        EXPECT_FALSE(slc::platform::loadCredential(kKey).has_value());
    } else {
        // Store unavailable (e.g. no secret-tool): lookups stay safe.
        EXPECT_FALSE(slc::platform::loadCredential(kKey).has_value());
        slc::platform::deleteCredential(kKey);
    }
}
