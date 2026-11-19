#include "slc/content/content_service.h"

#include <gtest/gtest.h>

namespace {

slc::content::Artwork makeArt(const std::string& appId,
                              slc::content::ArtworkKind kind,
                              const std::string& path)
{
    slc::content::Artwork a;
    a.appId = appId;
    a.gameName = "Game " + appId;
    a.kind = kind;
    a.path = path;
    return a;
}

class FakeProvider : public slc::content::IContentProvider {
  public:
    FakeProvider(std::string_view name,
                 std::vector<slc::content::Artwork> items)
        : m_name(name), m_items(std::move(items))
    {
    }

    std::string_view name() const noexcept override { return m_name; }

    std::vector<slc::content::Artwork>
    discover(const slc::content::DiscoveryContext&) override
    {
        ++calls;
        return m_items;
    }

    int calls = 0;

  private:
    std::string m_name;
    std::vector<slc::content::Artwork> m_items;
};

} // namespace

TEST(ContentService, EmptyProvidersIsError)
{
    slc::content::ContentService service;
    auto result = service.collect({});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, slc::ErrorCode::NotFound);
}

TEST(ContentService, ConcatenatesProvidersInOrder)
{
    using slc::content::ArtworkKind;
    slc::content::ContentService service;
    service.addProvider(std::make_unique<FakeProvider>(
        "a", std::vector{makeArt("1", ArtworkKind::Hero, "/a/hero.jpg")}));
    service.addProvider(std::make_unique<FakeProvider>(
        "b", std::vector{makeArt("2", ArtworkKind::Grid, "/b/grid.jpg")}));

    auto result = service.collect({});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].appId, "1");
    EXPECT_EQ((*result)[1].appId, "2");
}

TEST(ContentService, DeduplicatesIdenticalTriples)
{
    using slc::content::ArtworkKind;
    const auto dup = makeArt("1", ArtworkKind::Hero, "/same.jpg");
    slc::content::ContentService service;
    service.addProvider(std::make_unique<FakeProvider>(
        "a", std::vector{dup, dup, makeArt("1", ArtworkKind::Logo, "/other.png")}));
    service.addProvider(std::make_unique<FakeProvider>(
        "b", std::vector{dup, makeArt("2", ArtworkKind::Hero, "/b.jpg")}));

    auto result = service.collect({});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3u); // dup + logo + second hero
}

TEST(ContentService, SameAppDifferentPathKeepsBoth)
{
    using slc::content::ArtworkKind;
    slc::content::ContentService service;
    service.addProvider(std::make_unique<FakeProvider>(
        "a", std::vector{makeArt("1", ArtworkKind::Hero, "/local.jpg"),
                         makeArt("1", ArtworkKind::Hero, "/sgdb.jpg")}));

    auto result = service.collect({});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2u);
}

TEST(ContentService, StopTokenCancelsBetweenProviders)
{
    auto* first = new FakeProvider(
        "a", std::vector{makeArt("1", slc::content::ArtworkKind::Hero, "/a.jpg")});
    auto* second = new FakeProvider(
        "b", std::vector{makeArt("2", slc::content::ArtworkKind::Hero, "/b.jpg")});

    slc::content::ContentService service;
    service.addProvider(std::unique_ptr<FakeProvider>(first));
    service.addProvider(std::unique_ptr<FakeProvider>(second));

    std::stop_source source;
    source.request_stop();
    slc::content::DiscoveryContext ctx;
    ctx.stopToken = source.get_token();

    auto result = service.collect(ctx);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(first->calls, 1);
    EXPECT_EQ(second->calls, 0); // break happens between providers
    EXPECT_EQ(result->size(), 1u);
}
