/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
// Ported and substantially modified from Mozilla Readability's JSDOMParser.js.

#include "readability/lightweight_dom.hpp"
#include "dom_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace readability::detail {
namespace {
std::string upper(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return result;
}

std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

void append_code_point(std::string& out, unsigned int cp) {
  if (cp == 0U || cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) cp = 0xfffdU;
  if (cp <= 0x7fU) out.push_back(static_cast<char>(cp));
  else if (cp <= 0x7ffU) {
    out.push_back(static_cast<char>(0xc0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
  } else if (cp <= 0xffffU) {
    out.push_back(static_cast<char>(0xe0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
  } else {
    out.push_back(static_cast<char>(0xf0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
  }
}

bool is_void(std::string_view tag) {
  static const std::unordered_set<std::string> tags{
      "AREA", "BASE", "BR", "COL", "COMMAND", "EMBED", "HR", "IMG", "INPUT",
      "KEYGEN", "LINK", "META", "PARAM", "SOURCE", "TRACK", "WBR"};
  return tags.contains(std::string(tag));
}

std::vector<dom::Node*> as_nodes(const std::vector<LightweightNode*>& nodes) {
  return {nodes.begin(), nodes.end()};
}

void detach(LightweightNode& node) {
  if (node.parent_ == nullptr) return;
  auto& siblings = node.parent_->children_;
  const auto it = std::ranges::find(siblings, &node);
  if (it == siblings.end()) throw std::runtime_error("removeChild: node not found");
  siblings.erase(it);
  node.parent_ = nullptr;
}

void insert(LightweightNode& parent, LightweightNode& child, LightweightNode* reference) {
  if (&child == reference) return;
  std::vector<LightweightNode*> nodes;
  if (child.type_ == dom::NodeType::document_fragment) nodes = child.children_;
  else nodes.push_back(&child);
  if (reference != nullptr) {
    const auto position = std::ranges::find(parent.children_, reference);
    if (position == parent.children_.end()) throw std::runtime_error("insertBefore: reference node not found");
  }
  for (LightweightNode* node : nodes) detach(*node);
  auto position = parent.children_.end();
  if(reference != nullptr)position=std::ranges::find(parent.children_,reference);
  parent.children_.insert(position, nodes.begin(), nodes.end());
  for (LightweightNode* node : nodes) node->parent_ = &parent;
}

std::string serialize_children(const std::vector<LightweightNode*>& nodes) {
  std::string out;
  for (const LightweightNode* child : nodes) {
    if (child->type_ == dom::NodeType::text) {
      out += child->source_text_.value_or(encode_text(child->text_));
      continue;
    }
    if (child->type_ != dom::NodeType::element) continue;
    out += '<';
    out += lower(child->tag_);
    for (const auto& attribute : child->attributes_) {
      out += ' ';
      out += attribute.name;
      out += "=\"";
      out += encode_attribute(attribute.value);
      out += '"';
    }
    if (is_void(child->tag_) && child->children_.empty()) {
      out += "/>";
    } else {
      out += '>';
      out += serialize_children(child->children_);
      out += "</";
      out += lower(child->tag_);
      out += '>';
    }
  }
  return out;
}

std::string collect_text(const std::vector<LightweightNode*>& nodes) {
  std::string out;
  for (const LightweightNode* child : nodes) {
    if (child->type_ == dom::NodeType::text) out += child->text_;
    else out += collect_text(child->children_);
  }
  return out;
}
} // namespace

std::string encode_text(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

std::string encode_attribute(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&apos;";
    else out += c;
  }
  return out;
}

std::string decode_entities(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '&') { out += text[i++]; continue; }
    const std::size_t semicolon = text.find(';', i + 1U);
    if (semicolon == std::string_view::npos) { out += text[i++]; continue; }
    const std::string_view entity = text.substr(i + 1U, semicolon - i - 1U);
    if (entity == "lt") out += '<';
    else if (entity == "gt") out += '>';
    else if (entity == "amp") out += '&';
    else if (entity == "quot") out += '"';
    else if (entity == "apos") out += '\'';
    else if (!entity.empty() && entity.front() == '#') {
      unsigned int cp = 0U;
      std::string_view digits = entity.substr(1U);
      int base = 10;
      if (!digits.empty() && (digits.front() == 'x' || digits.front() == 'X')) {
        base = 16;
        digits.remove_prefix(1U);
      }
      const auto [ptr, error] = std::from_chars(digits.data(), digits.data() + digits.size(), cp, base);
      if (error == std::errc{} && ptr == digits.data() + digits.size()) append_code_point(out, cp);
      else { out.append(text.substr(i, semicolon - i + 1U)); }
    } else {
      out.append(text.substr(i, semicolon - i + 1U));
    }
    i = semicolon + 1U;
  }
  return out;
}

std::string resolve_url(std::string_view relative, std::string_view base) {
  const std::string rel(relative);
  if (rel.find("://") != std::string::npos || rel.starts_with("data:") || rel.starts_with("mailto:")) return rel;
  const std::string origin_base(base);
  const std::size_t scheme = origin_base.find("://");
  if (scheme == std::string::npos) return rel;
  const std::size_t path = origin_base.find('/', scheme + 3U);
  const std::string origin = path == std::string::npos ? origin_base : origin_base.substr(0, path);
  if (rel.starts_with("//")) return origin_base.substr(0, scheme + 1U) + rel;
  if (rel.starts_with('/')) return origin + rel;
  std::string directory = origin_base;
  const std::size_t slash = directory.rfind('/');
  if (slash != std::string::npos && slash >= scheme + 2U) directory.resize(slash + 1U);
  else directory += '/';
  std::string combined = directory + rel;
  const std::size_t root = combined.find('/', scheme + 3U);
  std::size_t pos = root;
  while ((pos = combined.find("/../", pos)) != std::string::npos && pos > root) {
    const std::size_t previous = combined.rfind('/', pos - 1U);
    if (previous == std::string::npos || previous < root) break;
    combined.erase(previous, pos + 3U - previous);
    pos = previous;
  }
  while ((pos = combined.find("/./", root)) != std::string::npos) combined.erase(pos, 2U);
  return combined;
}

LightweightNode::LightweightNode(LightweightDocument& document, dom::NodeType type, std::string tag)
    : document_(document), type_(type), tag_(upper(tag)) {}
dom::NodeType LightweightNode::type() const noexcept { return type_; }
std::string_view LightweightNode::tag_name() const noexcept { return tag_; }
void LightweightNode::rename(std::string_view tag) { if (type_ == dom::NodeType::element) tag_ = upper(tag); }
dom::Node* LightweightNode::parent() const noexcept { return parent_; }
std::vector<dom::Node*> LightweightNode::child_nodes() const { return as_nodes(children_); }
std::vector<dom::Node*> LightweightNode::children() const {
  std::vector<dom::Node*> result;
  for (auto* child : children_) if (child->type_ == dom::NodeType::element) result.push_back(child);
  return result;
}
std::vector<dom::Attribute> LightweightNode::attributes() const { return attributes_; }
std::optional<std::string> LightweightNode::attribute(std::string_view name) const {
  const auto it = std::ranges::find(attributes_, name, &dom::Attribute::name);
  return it == attributes_.end() ? std::nullopt : std::optional<std::string>(it->value);
}
void LightweightNode::set_attribute(std::string_view name, std::string_view value) {
  const auto it = std::ranges::find(attributes_, name, &dom::Attribute::name);
  if (it == attributes_.end()) attributes_.push_back({std::string(name), std::string(value)});
  else it->value = value;
  document_.base_uri_cache_.reset();
}
void LightweightNode::remove_attribute(std::string_view name) {
  std::erase_if(attributes_, [name](const auto& attr) { return attr.name == name; });
  document_.base_uri_cache_.reset();
}
std::string LightweightNode::text_content() const { return type_ == dom::NodeType::text ? text_ : collect_text(children_); }
void LightweightNode::set_text_content(std::string_view text) {
  if (type_ == dom::NodeType::text) { text_ = text; source_text_.reset(); return; }
  for (auto* child : children_) child->parent_ = nullptr;
  children_.clear();
  auto& child = static_cast<LightweightNode&>(document_.create_text_node(text));
  append_child(child);
}
std::string LightweightNode::inner_html() const { return type_ == dom::NodeType::text ? source_text_.value_or(encode_text(text_)) : serialize_children(children_); }
void LightweightNode::set_inner_html(std::string_view html) {
  LightweightDomParser parser;
  auto parsed = parser.parse(html);
  for (auto* child : children_) child->parent_ = nullptr;
  children_.clear();
  for (dom::Node* child : parsed->child_nodes()) append_child(document_.clone_into(*child));
}
dom::Node& LightweightNode::append_child(dom::Node& child) { return insert_before(child, nullptr); }
dom::Node& LightweightNode::insert_before(dom::Node& child, dom::Node* reference) {
  auto* actual = dynamic_cast<LightweightNode*>(&child);
  auto* ref = dynamic_cast<LightweightNode*>(reference);
  if (actual == nullptr || &actual->document_ != &document_) throw std::invalid_argument("node belongs to another DOM adapter");
  insert(*this, *actual, ref);
  return child;
}
dom::Node& LightweightNode::replace_child(dom::Node& replacement, dom::Node& old_child) {
  if (&replacement == &old_child) return old_child;
  insert_before(replacement, &old_child);
  old_child.remove();
  return old_child;
}
dom::Node& LightweightNode::remove() { detach(*this); return *this; }

LightweightDocument::LightweightDocument(std::string uri) : uri_(std::move(uri)) {}
dom::NodeType LightweightDocument::type() const noexcept { return dom::NodeType::document; }
std::string_view LightweightDocument::tag_name() const noexcept { return {}; }
void LightweightDocument::rename(std::string_view) {}
dom::Node* LightweightDocument::parent() const noexcept { return nullptr; }
std::vector<dom::Node*> LightweightDocument::child_nodes() const { return as_nodes(children_); }
std::vector<dom::Node*> LightweightDocument::children() const {
  std::vector<dom::Node*> result;
  for (auto* child : children_) if (child->type_ == dom::NodeType::element) result.push_back(child);
  return result;
}
std::vector<dom::Attribute> LightweightDocument::attributes() const { return {}; }
std::optional<std::string> LightweightDocument::attribute(std::string_view) const { return std::nullopt; }
void LightweightDocument::set_attribute(std::string_view, std::string_view) {}
void LightweightDocument::remove_attribute(std::string_view) {}
std::string LightweightDocument::text_content() const { return collect_text(children_); }
void LightweightDocument::set_text_content(std::string_view text) {
  for (auto* child : children_) child->parent_ = nullptr;
  children_.clear(); append_child(create_text_node(text)); refresh_special_nodes();
}
std::string LightweightDocument::inner_html() const { return serialize_children(children_); }
void LightweightDocument::set_inner_html(std::string_view html) {
  LightweightDomParser parser; auto parsed = parser.parse(html, uri_);
  for (auto* child : children_) child->parent_ = nullptr;
  children_.clear();
  for (dom::Node* child : parsed->child_nodes()) append_child(clone_into(*child));
  refresh_special_nodes();
}
dom::Node& LightweightDocument::append_child(dom::Node& child) { return insert_before(child, nullptr); }
dom::Node& LightweightDocument::insert_before(dom::Node& child, dom::Node* reference) {
  auto* actual = dynamic_cast<LightweightNode*>(&child);
  auto* ref = dynamic_cast<LightweightNode*>(reference);
  if (actual == nullptr || &actual->document_ != this) throw std::invalid_argument("node belongs to another DOM adapter");
  std::vector<LightweightNode*> nodes = actual->type_ == dom::NodeType::document_fragment ? actual->children_ : std::vector<LightweightNode*>{actual};
  if (ref != nullptr) { const auto position = std::ranges::find(children_, ref); if (position == children_.end()) throw std::runtime_error("insertBefore: reference node not found"); }
  for (auto* node : nodes) detach(*node);
  auto position = children_.end();
  if(ref != nullptr)position=std::ranges::find(children_,ref);
  children_.insert(position, nodes.begin(), nodes.end());
  for (auto* node : nodes) node->parent_ = nullptr; // Document is represented separately from LightweightNode.
  refresh_special_nodes();
  return child;
}
dom::Node& LightweightDocument::replace_child(dom::Node& replacement, dom::Node& old_child) {
  if (&replacement == &old_child) return old_child;
  insert_before(replacement, &old_child); old_child.remove(); refresh_special_nodes(); return old_child;
}
dom::Node& LightweightDocument::remove() { return *this; }
dom::Node* LightweightDocument::document_element() const noexcept { return root_; }
dom::Node* LightweightDocument::head() const noexcept { return head_; }
dom::Node* LightweightDocument::body() const noexcept { return body_; }
std::string LightweightDocument::title() const { return title_; }
std::string LightweightDocument::document_uri() const { return uri_; }
std::string LightweightDocument::base_uri() const {
  if (base_uri_cache_) return *base_uri_cache_;
  base_uri_cache_ = uri_;
  const auto bases = elements_by_tag_name("base");
  if (!bases.empty()) if (const auto href = bases.front()->attribute("href")) base_uri_cache_ = resolve_url(*href, uri_);
  return *base_uri_cache_;
}
dom::Node& LightweightDocument::create_element(std::string_view tag) { return make(dom::NodeType::element, std::string(tag)); }
dom::Node& LightweightDocument::create_text_node(std::string_view text) { auto& node = make(dom::NodeType::text); node.text_ = text; return node; }
dom::Node& LightweightDocument::create_document_fragment() { return make(dom::NodeType::document_fragment); }
LightweightNode& LightweightDocument::make(dom::NodeType type, std::string tag) {
  arena_.push_back(std::make_unique<LightweightNode>(*this, type, std::move(tag)));
  return *arena_.back();
}
LightweightNode& LightweightDocument::clone_into(const dom::Node& source) {
  auto& copy = make(source.type(), std::string(source.tag_name()));
  copy.text_ = source.type() == dom::NodeType::text ? source.text_content() : std::string{};
  if(const auto* lightweight=dynamic_cast<const LightweightNode*>(&source))copy.source_text_=lightweight->source_text_;
  copy.attributes_ = source.attributes();
  for (dom::Node* child : source.child_nodes()) copy.append_child(clone_into(*child));
  return copy;
}
void LightweightDocument::refresh_special_nodes() {
  root_ = head_ = body_ = nullptr; title_.clear();
  const auto all = elements_by_tag_name("*");
  for (dom::Node* node : all) {
    auto* actual = static_cast<LightweightNode*>(node);
    if (actual->tag_ == "HTML" && root_ == nullptr) root_ = actual;
    else if (actual->tag_ == "HEAD" && head_ == nullptr) head_ = actual;
    else if (actual->tag_ == "BODY" && body_ == nullptr) body_ = actual;
    else if (actual->tag_ == "TITLE" && title_.empty()) title_ = actual->text_content();
  }
  if (root_ == nullptr) for (auto* child : children_) if (child->tag_ == "HTML") { root_ = child; break; }
}
} // namespace readability::detail

namespace readability {
using detail::LightweightDocument;
using detail::LightweightNode;

struct LightweightDomParser::Impl {
  std::string html;
  std::size_t current{};
  std::string errors;
  LightweightDocument* document{};

  [[nodiscard]] char peek() const { return current < html.size() ? html[current] : '\0'; }
  [[nodiscard]] char next() { return current < html.size() ? html[current++] : '\0'; }
  bool match(std::string_view value) {
    if (current + value.size() > html.size()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
      const auto a = static_cast<unsigned char>(html[current + i]);
      const auto b = static_cast<unsigned char>(value[i]);
      if (std::tolower(a) != std::tolower(b)) return false;
    }
    current += value.size(); return true;
  }
  void error(std::string message) { errors += std::move(message); errors += '\n'; }
  void discard_to(std::string_view value) {
    const std::size_t found = html.find(value, current);
    current = found == std::string::npos ? html.size() : found + value.size();
  }
  std::string read_quoted(char quote) {
    const std::size_t end = html.find(quote, current);
    if (end == std::string::npos) { const std::string rest = html.substr(current); current = html.size(); return rest; }
    const std::string result = html.substr(current, end - current); current = end + 1U; return result;
  }
  void read_attribute(LightweightNode& node) {
    const std::size_t equal = html.find('=', current);
    if (equal == std::string::npos) { current = html.size(); return; }
    const std::string name = html.substr(current, equal - current); current = equal + 1U;
    if (name.empty()) return;
    const char quote = next();
    if (quote != '"' && quote != '\'') { error("Error reading attribute " + name + ", expecting quote"); return; }
    node.attributes_.push_back({name, detail::decode_entities(read_quoted(quote))});
  }
  LightweightNode* read_node() {
    const char first = next();
    if (first == '\0') return nullptr;
    if (first != '<') {
      --current;
      const std::size_t end = html.find('<', current);
      const std::size_t stop = end == std::string::npos ? html.size() : end;
      auto& text = document->make(dom::NodeType::text);
      text.source_text_=html.substr(current,stop-current);
      text.text_ = detail::decode_entities(*text.source_text_);
      current = stop; return &text;
    }
    if (match("![CDATA[")) {
      const std::size_t end = html.find("]]>", current);
      if (end == std::string::npos) { error("unclosed CDATA section"); return nullptr; }
      auto& text = document->make(dom::NodeType::text); text.text_ = html.substr(current, end - current); current = end + 3U; return &text;
    }
    if (peek() == '!' || peek() == '?') {
      ++current;
      if (match("--")) discard_to("-->");
      else {
        char c = next();
        while (c != '\0' && c != '>') { if (c == '"' || c == '\'') static_cast<void>(read_quoted(c)); c = next(); }
      }
      return &document->make(dom::NodeType::comment);
    }
    if (peek() == '/') { --current; return nullptr; }
    std::string tag;
    char c = next();
    while (c != '\0' && !std::isspace(static_cast<unsigned char>(c)) && c != '>' && c != '/') { tag += c; c = next(); }
    if (tag.empty()) return nullptr;
    const auto colon=tag.rfind(':');
    const std::string local_name=colon==std::string::npos?tag:tag.substr(colon+1U);
    auto& node = document->make(dom::NodeType::element, local_name);
    while (c != '\0' && c != '/' && c != '>') {
      while (current < html.size() && std::isspace(static_cast<unsigned char>(html[current]))) ++current;
      c = next();
      if (c != '/' && c != '>') { --current; read_attribute(node); }
    }
    bool closed = false;
    if (c == '/') { closed = true; if (next() != '>') { error("expected '>' to close " + tag); return nullptr; } }
    if (!closed) {
      while (LightweightNode* child = read_node()) if (child->type_ != dom::NodeType::comment) node.append_child(*child);
      const std::string closing = "</" + tag + ">";
      if (!match(closing)) { error("expected '" + closing + "'"); return nullptr; }
    }
    return &node;
  }
};

LightweightDomParser::LightweightDomParser() : impl_(std::make_unique<Impl>()) {}
LightweightDomParser::~LightweightDomParser() = default;
LightweightDomParser::LightweightDomParser(LightweightDomParser&&) noexcept = default;
LightweightDomParser& LightweightDomParser::operator=(LightweightDomParser&&) noexcept = default;

std::unique_ptr<dom::Document> LightweightDomParser::parse(std::string_view html, std::string_view url) {
  impl_->html = html; impl_->current = 0U; impl_->errors.clear();
  auto document = std::make_unique<LightweightDocument>(std::string(url));
  impl_->document = document.get();
  while (LightweightNode* child = impl_->read_node()) if (child->type_ != dom::NodeType::comment) document->children_.push_back(child);
  document->refresh_special_nodes();
  if (document->root_ != nullptr) {
    document->children_.erase(
        std::remove_if(document->children_.begin(), document->children_.end(),
                       [&](const LightweightNode* child) { return child != document->root_; }),
        document->children_.end());
  }
  return document;
}
const std::string& LightweightDomParser::error_state() const noexcept { return impl_->errors; }
} // namespace readability
