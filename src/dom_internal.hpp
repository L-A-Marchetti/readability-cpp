/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
// Internal implementation of the lightweight DOM port.
#pragma once

#include "readability/dom.hpp"

#include <memory>
#include <string>
#include <vector>

namespace readability::detail {

class LightweightDocument;

class LightweightNode final : public dom::Node {
public:
  LightweightNode(LightweightDocument& document, dom::NodeType type, std::string tag = {});
  [[nodiscard]] dom::NodeType type() const noexcept override;
  [[nodiscard]] std::string_view tag_name() const noexcept override;
  void rename(std::string_view tag) override;
  [[nodiscard]] dom::Node* parent() const noexcept override;
  [[nodiscard]] std::vector<dom::Node*> child_nodes() const override;
  [[nodiscard]] std::vector<dom::Node*> children() const override;
  [[nodiscard]] std::vector<dom::Attribute> attributes() const override;
  [[nodiscard]] std::optional<std::string> attribute(std::string_view name) const override;
  void set_attribute(std::string_view name, std::string_view value) override;
  void remove_attribute(std::string_view name) override;
  [[nodiscard]] std::string text_content() const override;
  void set_text_content(std::string_view text) override;
  [[nodiscard]] std::string inner_html() const override;
  void set_inner_html(std::string_view html) override;
  dom::Node& append_child(dom::Node& child) override;
  dom::Node& insert_before(dom::Node& child, dom::Node* reference) override;
  dom::Node& replace_child(dom::Node& replacement, dom::Node& old_child) override;
  dom::Node& remove() override;

  LightweightDocument& document_;
  dom::NodeType type_;
  std::string tag_;
  std::string text_;
  std::optional<std::string> source_text_;
  std::vector<dom::Attribute> attributes_;
  LightweightNode* parent_{nullptr};
  std::vector<LightweightNode*> children_;
};

class LightweightDocument final : public dom::Document {
public:
  explicit LightweightDocument(std::string uri);
  [[nodiscard]] dom::NodeType type() const noexcept override;
  [[nodiscard]] std::string_view tag_name() const noexcept override;
  void rename(std::string_view tag) override;
  [[nodiscard]] dom::Node* parent() const noexcept override;
  [[nodiscard]] std::vector<dom::Node*> child_nodes() const override;
  [[nodiscard]] std::vector<dom::Node*> children() const override;
  [[nodiscard]] std::vector<dom::Attribute> attributes() const override;
  [[nodiscard]] std::optional<std::string> attribute(std::string_view name) const override;
  void set_attribute(std::string_view name, std::string_view value) override;
  void remove_attribute(std::string_view name) override;
  [[nodiscard]] std::string text_content() const override;
  void set_text_content(std::string_view text) override;
  [[nodiscard]] std::string inner_html() const override;
  void set_inner_html(std::string_view html) override;
  dom::Node& append_child(dom::Node& child) override;
  dom::Node& insert_before(dom::Node& child, dom::Node* reference) override;
  dom::Node& replace_child(dom::Node& replacement, dom::Node& old_child) override;
  dom::Node& remove() override;
  [[nodiscard]] dom::Node* document_element() const noexcept override;
  [[nodiscard]] dom::Node* head() const noexcept override;
  [[nodiscard]] dom::Node* body() const noexcept override;
  [[nodiscard]] std::string title() const override;
  [[nodiscard]] std::string document_uri() const override;
  [[nodiscard]] std::string base_uri() const override;
  dom::Node& create_element(std::string_view tag) override;
  dom::Node& create_text_node(std::string_view text) override;
  dom::Node& create_document_fragment() override;

  LightweightNode& make(dom::NodeType type, std::string tag = {});
  LightweightNode& clone_into(const dom::Node& source);
  void refresh_special_nodes();

  std::string uri_;
  mutable std::optional<std::string> base_uri_cache_;
  std::string title_;
  LightweightNode* root_{nullptr};
  LightweightNode* head_{nullptr};
  LightweightNode* body_{nullptr};
  std::vector<LightweightNode*> children_;
  std::vector<std::unique_ptr<LightweightNode>> arena_;
};

[[nodiscard]] std::string encode_text(std::string_view text);
[[nodiscard]] std::string encode_attribute(std::string_view text);
[[nodiscard]] std::string decode_entities(std::string_view text);
[[nodiscard]] std::string resolve_url(std::string_view relative, std::string_view base);

} // namespace readability::detail
