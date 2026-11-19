#include "slc/content/vdf.h"

#include <cctype>
#include <cstddef>

namespace slc::content::vdf {
namespace {

class Parser {
  public:
    explicit Parser(std::string_view text) : m_text(text) {}

    Result<Node> parseDocument()
    {
        Node root;
        root.isObject = true;
        while (true) {
            skipWhitespaceAndComments();
            if (eof()) {
                return root;
            }
            if (peek() == '}') {
                return std::unexpected(fail("unexpected '}'"));
            }
            auto node = parsePair();
            if (!node) {
                return std::unexpected(node.error());
            }
            root.children.push_back(std::move(*node));
        }
    }

  private:
    [[nodiscard]] bool eof() const { return m_pos >= m_text.size(); }
    [[nodiscard]] char peek() const { return m_text[m_pos]; }

    void skipWhitespaceAndComments()
    {
        while (!eof()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (!eof() && peek() != '\n') {
                    ++m_pos;
                }
            } else {
                return;
            }
        }
    }

    Result<Node> parsePair()
    {
        auto key = parseToken();
        if (!key) {
            return std::unexpected(key.error());
        }
        skipWhitespaceAndComments();
        if (eof()) {
            return std::unexpected(fail("unexpected end of input after key '" + *key + "'"));
        }
        Node node;
        node.key = std::move(*key);
        if (peek() == '{') {
            ++m_pos;
            node.isObject = true;
            while (true) {
                skipWhitespaceAndComments();
                if (eof()) {
                    return std::unexpected(fail("unterminated object '" + node.key + "'"));
                }
                if (peek() == '}') {
                    ++m_pos;
                    break;
                }
                auto child = parsePair();
                if (!child) {
                    return std::unexpected(child.error());
                }
                node.children.push_back(std::move(*child));
            }
        } else {
            auto value = parseToken();
            if (!value) {
                return std::unexpected(value.error());
            }
            node.value = std::move(*value);
        }
        return node;
    }

    Result<std::string> parseToken()
    {
        if (peek() == '"') {
            return parseQuoted();
        }
        return parseBare();
    }

    Result<std::string> parseQuoted()
    {
        ++m_pos; // opening quote
        std::string out;
        while (true) {
            if (eof()) {
                return std::unexpected(fail("unterminated quoted string"));
            }
            const char c = m_text[m_pos++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (eof()) {
                    return std::unexpected(fail("escape at end of input"));
                }
                const char next = m_text[m_pos++];
                switch (next) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                case '\'': out += '\''; break;
                default:   out += next; break; // keep unknown escapes verbatim
                }
            } else {
                out += c;
            }
        }
    }

    Result<std::string> parseBare()
    {
        const std::size_t start = m_pos;
        while (!eof()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}') {
                break;
            }
            ++m_pos;
        }
        if (m_pos == start) {
            return std::unexpected(fail(std::string("expected token, found '") + peek() + "'"));
        }
        return std::string(m_text.substr(start, m_pos - start));
    }

    [[nodiscard]] Error fail(const std::string& message) const
    {
        return Error::make(ErrorCode::Malformed,
                           "vdf: " + message + " (offset " + std::to_string(m_pos) + ")");
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
};

[[nodiscard]] bool keyEquals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

const Node* Node::find(std::string_view childKey) const
{
    for (const Node& child : children) {
        if (keyEquals(child.key, childKey)) {
            return &child;
        }
    }
    return nullptr;
}

std::string_view Node::get(std::string_view childKey, std::string_view fallback) const
{
    const Node* child = find(childKey);
    if (child == nullptr || child->isObject) {
        return fallback;
    }
    return child->value;
}

Result<Node> parse(std::string_view text)
{
    // Strip UTF-8 BOM if present.
    if (text.size() >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF') {
        text.remove_prefix(3);
    }
    return Parser(text).parseDocument();
}

} // namespace slc::content::vdf
