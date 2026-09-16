// Readability++ DOM abstraction.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace readability::dom {

enum class NodeType { element = 1, text = 3, comment = 8, document = 9, document_fragment = 11 };

struct Attribute {
  std::string name;
  std::string value;
  friend bool operator==(const Attribute&, const Attribute&) = default;
};

// Nodes are owned by their Document. Pointers returned by this interface are
// non-owning and remain valid until the Document is destroyed, even if a node
// is detached. This mirrors the lifetime Readability needs while permitting
// adapters over arena-, ref-counted-, and browser-owned DOMs.
class Node {
public:
  virtual ~Node() = default;
  [[nodiscard]] virtual NodeType type() const noexcept = 0;
  [[nodiscard]] virtual std::string_view tag_name() const noexcept = 0; // uppercase for elements
  virtual void rename(std::string_view tag) = 0;
  [[nodiscard]] virtual Node* parent() const noexcept = 0;
  [[nodiscard]] virtual std::vector<Node*> child_nodes() const = 0;
  [[nodiscard]] virtual std::vector<Node*> children() const = 0;
  [[nodiscard]] virtual std::vector<Attribute> attributes() const = 0;
  [[nodiscard]] virtual std::optional<std::string> attribute(std::string_view name) const = 0;
  virtual void set_attribute(std::string_view name, std::string_view value) = 0;
  virtual void remove_attribute(std::string_view name) = 0;
  [[nodiscard]] virtual std::string text_content() const = 0;
  virtual void set_text_content(std::string_view text) = 0;
  [[nodiscard]] virtual std::string inner_html() const = 0;
  virtual void set_inner_html(std::string_view html) = 0;
  virtual Node& append_child(Node& child) = 0;
  virtual Node& insert_before(Node& child, Node* reference) = 0;
  virtual Node& replace_child(Node& replacement, Node& old_child) = 0;
  virtual Node& remove() = 0;

  [[nodiscard]] Node* first_child() const;
  [[nodiscard]] Node* last_child() const;
  [[nodiscard]] Node* first_element_child() const;
  [[nodiscard]] Node* last_element_child() const;
  [[nodiscard]] Node* previous_sibling() const;
  [[nodiscard]] Node* next_sibling() const;
  [[nodiscard]] Node* previous_element_sibling() const;
  [[nodiscard]] Node* next_element_sibling() const;
  [[nodiscard]] bool has_attribute(std::string_view name) const;
  [[nodiscard]] std::string class_name() const;
  [[nodiscard]] std::string id() const;
  [[nodiscard]] std::vector<Node*> elements_by_tag_name(std::string_view tag) const;
};

class Document : public Node {
public:
  [[nodiscard]] virtual Node* document_element() const noexcept = 0;
  [[nodiscard]] virtual Node* head() const noexcept = 0;
  [[nodiscard]] virtual Node* body() const noexcept = 0;
  [[nodiscard]] virtual std::string title() const = 0;
  [[nodiscard]] virtual std::string document_uri() const = 0;
  [[nodiscard]] virtual std::string base_uri() const = 0;
  virtual Node& create_element(std::string_view tag) = 0;
  virtual Node& create_text_node(std::string_view text) = 0;
  virtual Node& create_document_fragment() = 0;
};

[[nodiscard]] bool is_element(const Node* node) noexcept;
[[nodiscard]] bool tag_is(const Node* node, std::string_view uppercase_tag) noexcept;

} // namespace readability::dom
