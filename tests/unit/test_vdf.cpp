#include "slc/content/vdf.h"

#include <gtest/gtest.h>

using slc::ErrorCode;
using slc::content::vdf::Node;
using slc::content::vdf::parse;

TEST(Vdf, ParsesFlatPairs)
{
    auto doc = parse(R"("root" { "a" "1" "b" "two" })");
    ASSERT_TRUE(doc.has_value());
    const Node* root = doc->find("root");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->get("a"), "1");
    EXPECT_EQ(root->get("b"), "two");
}

TEST(Vdf, ParsesNestedObjects)
{
    auto doc = parse(R"(
"libraryfolders"
{
	"0"
	{
		"path"		"D:\\Steam"
		"apps" { "440" "1" }
	}
})");
    ASSERT_TRUE(doc.has_value());
    const Node* zero = doc->find("libraryfolders")->find("0");
    ASSERT_NE(zero, nullptr);
    EXPECT_EQ(zero->get("path"), "D:\\Steam");
    EXPECT_EQ(zero->find("apps")->get("440"), "1");
}

TEST(Vdf, SkipsLineComments)
{
    auto doc = parse("// comment\n\"a\" \"1\" // trailing\n\"b\" \"2\"");
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->get("a"), "1");
    EXPECT_EQ(doc->get("b"), "2");
}

TEST(Vdf, HandlesEscapedQuotes)
{
    auto doc = parse(R"("name" "say \"hi\" now")");
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->get("name"), "say \"hi\" now");
}

TEST(Vdf, HandlesBareTokens)
{
    auto doc = parse("a 1\nb two\n");
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->get("a"), "1");
    EXPECT_EQ(doc->get("b"), "two");
}

TEST(Vdf, StripsUtf8Bom)
{
    auto doc = parse("\xEF\xBB\xBF\"a\" \"1\"");
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->get("a"), "1");
}

TEST(Vdf, KeysAreCaseInsensitive)
{
    auto doc = parse("\"AppState\" { \"AppId\" \"440\" }");
    ASSERT_TRUE(doc.has_value());
    const Node* state = doc->find("appstate");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->get("appid"), "440");
}

TEST(Vdf, EmptyObjectIsNotLeaf)
{
    auto doc = parse("\"a\" {} \"b\" \"\"");
    ASSERT_TRUE(doc.has_value());
    EXPECT_TRUE(doc->find("a")->isObject);
    EXPECT_FALSE(doc->find("b")->isObject);
}

TEST(Vdf, GetReturnsFallback)
{
    auto doc = parse("\"a\" \"1\"");
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->get("missing", "fb"), "fb");
    // Objects never masquerade as leaf values.
    auto doc2 = parse("\"obj\" { \"x\" \"1\" }");
    EXPECT_EQ(doc2->get("obj", "fb"), "fb");
}

TEST(Vdf, MalformedInputsFail)
{
    EXPECT_EQ(parse("\"a\" {").error().code, ErrorCode::Malformed);
    EXPECT_EQ(parse("\"a\"").error().code, ErrorCode::Malformed);
    EXPECT_EQ(parse("}").error().code, ErrorCode::Malformed);
    EXPECT_EQ(parse("\"a\" \"unterminated").error().code, ErrorCode::Malformed);
}

TEST(Vdf, EmptyDocumentIsValid)
{
    auto doc = parse("");
    ASSERT_TRUE(doc.has_value());
    EXPECT_TRUE(doc->children.empty());
}
