// SteamGridDbProvider behaviour tests via an injected mock transport —
// no cpr, no network (doc 9.2: 429 backoff, malformed JSON, timeout,
// missing key).
#include "slc/content/steamgriddb_provider.h"

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

using slc::Error;
using slc::ErrorCode;
using slc::Result;
using slc::content::ArtworkKind;
using slc::content::DiscoveryContext;
using slc::content::HttpRequest;
using slc::content::HttpResponse;
using slc::content::IHttpTransport;
using slc::content::SgdbOptions;
using slc::content::SteamGridDbProvider;
using slc::content::TokenBucket;

namespace {

class MockTransport final : public IHttpTransport {
  public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    Handler handler;
    std::vector<HttpRequest> requests;

    Result<HttpResponse> get(const HttpRequest& req,
                             std::stop_token) override
    {
        requests.push_back(req);
        if (handler) {
            return handler(req);
        }
        return std::unexpected(
            Error::make(ErrorCode::Timeout, "no handler"));
    }
};

struct Fixture {
    std::filesystem::path dir;
    MockTransport* transport;
    std::unique_ptr<SteamGridDbProvider> provider;

    explicit Fixture(std::initializer_list<std::string> appIds,
                     std::unordered_set<ArtworkKind> kinds = {})
    {
        dir = std::filesystem::temp_directory_path() /
              ("slc_sgdb_test_" +
               std::to_string(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count()));
        std::filesystem::create_directories(dir);
        auto mt = std::make_unique<MockTransport>();
        transport = mt.get();
        SgdbOptions opt;
        opt.apiKey = "test-key";
        opt.cacheDir = dir;
        opt.appIds.insert(appIds.begin(), appIds.end());
        opt.includeKinds = std::move(kinds);
        opt.ratePerSec = 10000.0; // fast tests: effectively no pacing
        opt.burst = 10000.0;
        opt.backoffBase = std::chrono::milliseconds(1);
        opt.maxRetryAfter = std::chrono::milliseconds(5);
        provider = std::make_unique<SteamGridDbProvider>(std::move(mt),
                                                       std::move(opt));
    }

    ~Fixture()
    {
        provider.reset();
        std::filesystem::remove_all(dir);
    }
};

HttpResponse jsonOk(const std::string& url)
{
    HttpResponse r;
    r.status = 200;
    r.body = std::string("{\"success\":true,\"data\":[{\"id\":1,\"url\":\"") +
             url + "\"}]}";
    return r;
}

HttpResponse imageOk()
{
    HttpResponse r;
    r.status = 200;
    r.body = "fake-png-bytes";
    return r;
}

} // namespace

TEST(SgdbProvider, EmptyApiKeyDoesNothing)
{
    auto mt = std::make_unique<MockTransport>();
    auto* mtPtr = mt.get();
    SgdbOptions opt; // no apiKey
    opt.cacheDir = std::filesystem::temp_directory_path() / "slc_sgdb_nokey";
    opt.appIds = {"440"};
    SteamGridDbProvider p(std::move(mt), std::move(opt));
    DiscoveryContext ctx;
    EXPECT_TRUE(p.discover(ctx).empty());
    EXPECT_TRUE(mtPtr->requests.empty());
}

TEST(SgdbProvider, HappyPathCachesAndEmits)
{
    Fixture f({"440"}, {ArtworkKind::Hero});
    f.transport->handler = [](const HttpRequest& req) {
        if (req.url.find("cdn.") != std::string::npos) {
            return imageOk();
        }
        return jsonOk("https://cdn.steamgriddb.com/x.png");
    };

    DiscoveryContext ctx;
    const auto art = f.provider->discover(ctx);
    ASSERT_EQ(art.size(), 1u);
    EXPECT_EQ(art[0].appId, "440");
    EXPECT_EQ(art[0].kind, ArtworkKind::Hero);
    EXPECT_EQ(art[0].source, "steamgriddb");
    EXPECT_TRUE(std::filesystem::exists(art[0].path));
    ASSERT_EQ(f.transport->requests.size(), 2u);
    EXPECT_NE(f.transport->requests[0].url.find("heroes/game/440"),
              std::string::npos);
    // Bearer auth header present.
    bool sawAuth = false;
    for (const auto& [k, v] : f.transport->requests[0].headers) {
        sawAuth = sawAuth ||
                  (k == "Authorization" && v == "Bearer test-key");
    }
    EXPECT_TRUE(sawAuth);

    // Second discover: cache hit, zero requests.
    const auto art2 = f.provider->discover(ctx);
    EXPECT_EQ(art2.size(), 1u);
    EXPECT_TRUE(f.transport->requests.empty() ||
                f.transport->requests.size() == 2u);
}

TEST(SgdbProvider, NotFoundIsNegativeCached)
{
    Fixture f({"440"}, {ArtworkKind::Hero});
    int calls = 0;
    f.transport->handler = [&](const HttpRequest&) {
        ++calls;
        HttpResponse r;
        r.status = 404;
        return r;
    };
    DiscoveryContext ctx;
    EXPECT_TRUE(f.provider->discover(ctx).empty());
    EXPECT_TRUE(f.provider->discover(ctx).empty());
    EXPECT_EQ(calls, 1); // second call skipped by negative cache
}

TEST(SgdbProvider, MalformedJsonIsNegativeCached)
{
    Fixture f({"440"}, {ArtworkKind::Grid});
    int calls = 0;
    f.transport->handler = [&](const HttpRequest&) {
        ++calls;
        HttpResponse r;
        r.status = 200;
        r.body = "{not json";
        return r;
    };
    DiscoveryContext ctx;
    EXPECT_TRUE(f.provider->discover(ctx).empty());
    EXPECT_TRUE(f.provider->discover(ctx).empty());
    EXPECT_EQ(calls, 1);
}

TEST(SgdbProvider, RateLimit429RetriesThenSucceeds)
{
    Fixture f({"440"}, {ArtworkKind::Hero});
    int apiCalls = 0;
    f.transport->handler = [&](const HttpRequest& req) {
        if (req.url.find("cdn.") != std::string::npos) {
            return imageOk();
        }
        ++apiCalls;
        if (apiCalls == 1) {
            HttpResponse r;
            r.status = 429;
            r.headers["retry-after"] = "0";
            return r;
        }
        return jsonOk("https://cdn.steamgriddb.com/x.png");
    };
    DiscoveryContext ctx;
    const auto art = f.provider->discover(ctx);
    EXPECT_EQ(art.size(), 1u);
    EXPECT_EQ(apiCalls, 2);
}

TEST(SgdbProvider, ServerErrorRetriesThenDegradesSilently)
{
    Fixture f({"440"}, {ArtworkKind::Hero});
    int calls = 0;
    f.transport->handler = [&](const HttpRequest&) {
        ++calls;
        HttpResponse r;
        r.status = 500;
        return r;
    };
    DiscoveryContext ctx;
    EXPECT_TRUE(f.provider->discover(ctx).empty());
    EXPECT_EQ(calls, 3); // 1 + maxRetries(2)
}

TEST(SgdbProvider, TransportTimeoutDegradesSilently)
{
    Fixture f({"440"}, {ArtworkKind::Hero});
    f.transport->handler = nullptr; // -> unexpected Timeout
    DiscoveryContext ctx;
    EXPECT_TRUE(f.provider->discover(ctx).empty());
}

TEST(SgdbProvider, StopTokenCancelsCleanly)
{
    Fixture f({"440", "730", "220"}, {ArtworkKind::Hero});
    f.transport->handler = [](const HttpRequest&) {
        return jsonOk("https://cdn.steamgriddb.com/x.png");
    };
    DiscoveryContext ctx;
    std::stop_source ss;
    ctx.stopToken = ss.get_token();
    ss.request_stop();
    EXPECT_TRUE(f.provider->discover(ctx).empty());
}

TEST(TokenBucket, BurstThenPacing)
{
    TokenBucket b(100.0, 3.0);
    EXPECT_TRUE(b.tryAcquire());
    EXPECT_TRUE(b.tryAcquire());
    EXPECT_TRUE(b.tryAcquire());
    EXPECT_FALSE(b.tryAcquire()); // burst exhausted
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    EXPECT_TRUE(b.tryAcquire()); // ~1.5 tokens refilled at 100/s
}

TEST(TokenBucket, AcquireHonoursStopToken)
{
    TokenBucket b(0.001, 0.0); // effectively never refills
    std::stop_source ss;
    ss.request_stop();
    EXPECT_FALSE(b.acquire(ss.get_token()));
}
