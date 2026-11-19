#include "slc/common/error.h"

#include <gtest/gtest.h>

TEST(Error, ToStringCoversAllCodes)
{
    using slc::ErrorCode;
    for (auto code : {ErrorCode::NotFound, ErrorCode::PermissionDenied,
                      ErrorCode::Malformed, ErrorCode::IoError,
                      ErrorCode::Timeout, ErrorCode::Unsupported}) {
        EXPECT_FALSE(slc::toString(code).empty()) << static_cast<int>(code);
    }
}

TEST(Error, MakeCapturesCodeMessageAndLocation)
{
    const auto err = slc::Error::make(slc::ErrorCode::Malformed, "bad data");
    EXPECT_EQ(err.code, slc::ErrorCode::Malformed);
    EXPECT_EQ(err.message, "bad data");
    EXPECT_GT(err.where.line(), 0u);
}
