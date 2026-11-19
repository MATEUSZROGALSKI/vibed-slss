#include "slc/content/hltb.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <vector>

using slc::content::HltbResolver;
using slc::content::HttpRequest;
using slc::content::HttpResponse;
using slc::content::IHttpTransport;
using slc::Error;
using slc::ErrorCode;
using slc::Result;

namespace {

const std::filesystem::path& tmpDir()
{
    static const std::filesystem::path dir = [] {
        const auto p =
            std::filesystem::temp_directory_path() /
            ("slc_hltb_test_" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count()));
        std::filesystem::create_directories(p);
        return p;
    }();
    return dir;
}

class MockTransport final : public IHttpTransport {
  public:
    std::function<HttpResponse(const HttpRequest&)> getHandler;
    std::function<HttpResponse(const HttpRequest&, const std::string&)>
        postHandler;
    int posts = 0;

    Result<HttpResponse> get(const HttpRequest& req,
                             std::stop_token) override
    {
        if (getHandler) {
            return getHandler(req);
        }
        return std::unexpected(
            Error::make(ErrorCode::Timeout, "no get handler"));
    }

    Result<HttpResponse> post(const HttpRequest& req, const std::string& body,
                              std::stop_token) override
    {
        ++posts;
        if (postHandler) {
            return postHandler(req, body);
        }
        return std::unexpected(
            Error::make(ErrorCode::Timeout, "no post handler"));
    }
};

HttpResponse ok(std::string body)
{
    HttpResponse r;
    r.status = 200;
    r.body = std::move(body);
    return r;
}

TEST(Hltb, ResolvesMainMinutes)
{
    auto mt = std::make_unique<MockTransport>();
    auto* mtp = mt.get();
    mtp->getHandler = [](const HttpRequest&) {
        return ok(R"({"token":"abc"})");
    };
    mtp->postHandler = [](const HttpRequest&, const std::string&) {
        return ok(
            R"({"count":1,"data":[{"game_name":"Blasphemous","comp_main":50477,"comp_plus":72179,"comp_100":113634}]})");
    };
    HltbResolver r(std::move(mt), tmpDir());
    const auto mins = r.resolve("774560", "Blasphemous", {});
    ASSERT_TRUE(mins.has_value());
    EXPECT_EQ(*mins, 841u); // 50477s / 60
    EXPECT_EQ(r.lookup("774560"), mins);
    EXPECT_EQ(mtp->posts, 1);
}

TEST(Hltb, PrefersExactNameMatchOverFirstResult)
{
    auto mt = std::make_unique<MockTransport>();
    auto* mtp = mt.get();
    mtp->getHandler = [](const HttpRequest&) {
        return ok(R"({"token":"abc"})");
    };
    mtp->postHandler = [](const HttpRequest&, const std::string&) {
        return ok(
            R"({"count":2,"data":[{"game_name":"Dishonored 2","comp_main":60000},{"game_name":"Dishonored","comp_main":30000}]})");
    };
    HltbResolver r(std::move(mt), tmpDir());
    EXPECT_EQ(r.resolve("205100", "Dishonored", {}), std::optional(500u));
    EXPECT_EQ(mtp->posts, 1);
}

TEST(Hltb, EmptyResultNegativeCaches)
{
    auto mt = std::make_unique<MockTransport>();
    auto* mtp = mt.get();
    mtp->getHandler = [](const HttpRequest&) {
        return ok(R"({"token":"abc"})");
    };
    mtp->postHandler = [](const HttpRequest&, const std::string&) {
        return ok(R"({"count":0,"data":[]})");
    };
    HltbResolver r(std::move(mt), tmpDir());
    EXPECT_FALSE(r.resolve("1", "Obscure", {}).has_value());
    EXPECT_FALSE(r.needsResolve("1")); // negative-cached: no retry next time
    EXPECT_FALSE(r.resolve("1", "Obscure", {}).has_value());
    EXPECT_EQ(mtp->posts, 1);
}

TEST(Hltb, CachePersistsAcrossInstances)
{
    const auto dir = tmpDir() / "persist";
    {
        auto mt = std::make_unique<MockTransport>();
        auto* mtp = mt.get();
        mtp->getHandler = [](const HttpRequest&) {
            return ok(R"({"token":"abc"})");
        };
        mtp->postHandler = [](const HttpRequest&, const std::string&) {
            return ok(
                R"({"count":1,"data":[{"game_name":"Portal","comp_main":11000}]})");
        };
        HltbResolver r(std::move(mt), dir);
        ASSERT_EQ(r.resolve("400", "Portal", {}), std::optional(183u));
        r.save();
    }
    HltbResolver r2(nullptr, dir);
    EXPECT_EQ(r2.lookup("400"), std::optional(183u));
    // Cached value resolves without any transport at all.
    EXPECT_EQ(r2.resolve("400", "Portal", {}), std::optional(183u));
}

TEST(Hltb, NoTransportStillServesCacheOnly)
{
    HltbResolver r(nullptr, tmpDir() / "none");
    EXPECT_FALSE(r.lookup("42").has_value());
    EXPECT_FALSE(r.resolve("42", "Whatever", {}).has_value());
    // No transport -> no negative entry written.
    EXPECT_TRUE(r.needsResolve("42"));
}

} // namespace
