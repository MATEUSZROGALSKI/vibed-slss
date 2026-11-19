#pragma once

#include "slc/common/error.h"

#include <string>
#include <string_view>
#include <vector>

namespace slc::content::vdf {

/**
 * A node in a Valve KeyValues ("VDF") document.
 *
 * A node is either an object (children, `isObject() == true`) or a leaf
 * (`value` holds the unescaped string). An empty object (`"k" {}`) and an
 * empty leaf (`"k" ""`) are distinguishable via isObject().
 */
struct Node {
    std::string key;
    std::string value;
    std::vector<Node> children;
    bool isObject = false;

    /** First direct child with the given key, or nullptr. Case-insensitive,
     * matching Steam's own KeyValues semantics. */
    [[nodiscard]] const Node* find(std::string_view childKey) const;

    /** Leaf value of first direct child `childKey`, or `fallback`. */
    [[nodiscard]] std::string_view get(std::string_view childKey,
                                       std::string_view fallback = {}) const;
};

/**
 * Parses a VDF document. The returned node is a synthetic root whose children
 * are the document's top-level pairs (libraryfolders.vdf: a single "libraryfolders"
 * object; appmanifest: a single "AppState" object).
 *
 * Tolerates `//` comments, bare (unquoted) tokens, and escaped quotes.
 * Malformed input yields ErrorCode::Malformed with position context.
 */
[[nodiscard]] Result<Node> parse(std::string_view text);

} // namespace slc::content::vdf
