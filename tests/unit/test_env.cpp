#include "slc/common/env.h"

#include "env_guard.h"

#include <gtest/gtest.h>

TEST(Env, GetReturnsUnsetForMissing)
{
    test::unsetEnv("SLC_UNIT_TEST_MISSING");
    EXPECT_FALSE(slc::env::get("SLC_UNIT_TEST_MISSING").has_value());
}

TEST(Env, GetReturnsValue)
{
    test::EnvGuard guard("SLC_UNIT_TEST_VALUE", "hello");
    auto value = slc::env::get("SLC_UNIT_TEST_VALUE");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "hello");
}

TEST(Env, GetTreatsEmptyAsUnset)
{
    test::EnvGuard guard("SLC_UNIT_TEST_EMPTY", "");
    EXPECT_FALSE(slc::env::get("SLC_UNIT_TEST_EMPTY").has_value());
}

TEST(Env, GetPathConvertsValue)
{
    test::EnvGuard guard("SLC_UNIT_TEST_PATH", "/tmp/slc");
    auto path = slc::env::getPath("SLC_UNIT_TEST_PATH");
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->string(), "/tmp/slc");
}

TEST(Env, GetPathReturnsUnsetForMissing)
{
    test::unsetEnv("SLC_UNIT_TEST_NOPATH");
    EXPECT_FALSE(slc::env::getPath("SLC_UNIT_TEST_NOPATH").has_value());
}

TEST(Env, IsTruthyRecognisesTrueValues)
{
    for (const char* v : {"1", "true", "yes", "on"}) {
        test::EnvGuard guard("SLC_UNIT_TEST_FLAG", v);
        EXPECT_TRUE(slc::env::isTruthy("SLC_UNIT_TEST_FLAG")) << v;
    }
}

TEST(Env, IsTruthyRejectsOtherValues)
{
    for (const char* v : {"0", "false", "no", "off", "random"}) {
        test::EnvGuard guard("SLC_UNIT_TEST_FLAG", v);
        EXPECT_FALSE(slc::env::isTruthy("SLC_UNIT_TEST_FLAG")) << v;
    }
    test::unsetEnv("SLC_UNIT_TEST_FLAG");
    EXPECT_FALSE(slc::env::isTruthy("SLC_UNIT_TEST_FLAG"));
}

TEST(Env, TestHooksRequireOptIn)
{
    test::EnvGuard guard("SLC_TEST_UNIT_HOOK", "1");
    slc::env::setAllowTestHooks(false);
    EXPECT_FALSE(slc::env::allowTestHooks());
    EXPECT_FALSE(slc::env::getTestHook("SLC_TEST_UNIT_HOOK").has_value());
    EXPECT_FALSE(slc::env::testHookTruthy("SLC_TEST_UNIT_HOOK"));
}

TEST(Env, TestHooksPassThroughWhenEnabled)
{
    test::EnvGuard guard("SLC_TEST_UNIT_HOOK", "yes");
    slc::env::setAllowTestHooks(true);
    EXPECT_TRUE(slc::env::allowTestHooks());
    auto hook = slc::env::getTestHook("SLC_TEST_UNIT_HOOK");
    ASSERT_TRUE(hook.has_value());
    EXPECT_EQ(*hook, "yes");
    EXPECT_TRUE(slc::env::testHookTruthy("SLC_TEST_UNIT_HOOK"));
    slc::env::setAllowTestHooks(false); // leave the gate closed for other tests
}
