// SPDX-License-Identifier: Apache-2.0
#include "readability/dom.hpp"

#include <algorithm>
#include <cctype>

namespace readability::dom {
namespace {
std::string upper(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return result;
}

Node* adjacent(const Node& node, bool next, bool elements_only) {
  Node* const parent = node.parent();
  if (parent == nullptr) return nullptr;
  const auto siblings = elements_only ? parent->children() : parent->child_nodes();
  const auto it = std::ranges::find(siblings, &node);
  if (it == siblings.end()) return nullptr;
  if (next) {
    const auto following = std::next(it);
    return following == siblings.end() ? nullptr : *following;
  }
  return it == siblings.begin() ? nullptr : *std::prev(it);
}
} // namespace

Node* Node::first_child() const { const auto v = child_nodes(); return v.empty() ? nullptr : v.front(); }
Node* Node::last_child() const { const auto v = child_nodes(); return v.empty() ? nullptr : v.back(); }
Node* Node::first_element_child() const { const auto v = children(); return v.empty() ? nullptr : v.front(); }
Node* Node::last_element_child() const { const auto v = children(); return v.empty() ? nullptr : v.back(); }
Node* Node::previous_sibling() const { return adjacent(*this, false, false); }
Node* Node::next_sibling() const { return adjacent(*this, true, false); }
Node* Node::previous_element_sibling() const { return adjacent(*this, false, true); }
Node* Node::next_element_sibling() const { return adjacent(*this, true, true); }
bool Node::has_attribute(std::string_view name) const { return attribute(name).has_value(); }
std::string Node::class_name() const { return attribute("class").value_or(""); }
std::string Node::id() const { return attribute("id").value_or(""); }

std::vector<Node*> Node::elements_by_tag_name(std::string_view tag) const {
  const std::string wanted = upper(tag);
  std::vector<Node*> result;
  const bool wildcard = wanted == "*";
  const auto visit = [&](const auto& self, const Node& root) -> void {
    for (Node* child : root.children()) {
      if (wildcard || child->tag_name() == wanted) result.push_back(child);
      self(self, *child);
    }
  };
  visit(visit, *this);
  return result;
}

bool is_element(const Node* node) noexcept { return node != nullptr && node->type() == NodeType::element; }
bool tag_is(const Node* node, std::string_view uppercase_tag) noexcept {
  return is_element(node) && node->tag_name() == uppercase_tag;
}
} // namespace readability::dom
