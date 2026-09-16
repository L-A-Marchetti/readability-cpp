/*
 * Copyright (c) 2010 Arc90 Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Ported from Mozilla Readability's Readability-readerable.js.
#include "readability/readability.hpp"
#include "text_length.hpp"

#include <cmath>
#include <regex>
#include <unordered_set>

namespace readability {
namespace {
const std::regex unlikely("-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|pagination|pager|popup|yom-remote", std::regex::icase);
const std::regex maybe("and|article|body|column|content|main|mathjax|shadow", std::regex::icase);

bool visible(const dom::Node& node) {
  const std::string style = node.attribute("style").value_or("");
  const bool display_none = std::regex_search(style, std::regex(R"((^|;)\s*display\s*:\s*none\s*(;|$))", std::regex::icase));
  return !display_none && !node.has_attribute("hidden") &&
         (!node.has_attribute("aria-hidden") || node.attribute("aria-hidden") != "true" ||
          node.class_name().find("fallback-image") != std::string::npos);
}

bool has_ancestor(const dom::Node& node, std::string_view tag) {
  for (auto* parent = node.parent(); parent != nullptr; parent = parent->parent()) if (parent->tag_name() == tag) return true;
  return false;
}
} // namespace

bool is_probably_readerable(const dom::Document& document, ReaderableOptions options) {
  if (!options.visibility_checker) options.visibility_checker = visible;
  std::vector<dom::Node*> nodes;
  for (dom::Node* node : document.elements_by_tag_name("*")) {
    if (node->tag_name() == "P" || node->tag_name() == "PRE" || node->tag_name() == "ARTICLE") nodes.push_back(node);
  }
  for (dom::Node* br : document.elements_by_tag_name("br")) {
    if (dom::tag_is(br->parent(), "DIV") && std::ranges::find(nodes, br->parent()) == nodes.end()) nodes.push_back(br->parent());
  }
  double score = 0.0;
  for (const dom::Node* node : nodes) {
    if (!options.visibility_checker(*node)) continue;
    const std::string match = node->class_name() + " " + node->id();
    if (std::regex_search(match, unlikely) && !std::regex_search(match, maybe)) continue;
    if (node->tag_name() == "P" && (dom::tag_is(node->parent(), "LI") || has_ancestor(*node, "LI"))) continue;
    const std::size_t length = detail::javascript_string_length(node->text_content());
    if (length < options.minimum_content_length) continue;
    score += std::sqrt(static_cast<double>(length - options.minimum_content_length));
    if (score > options.minimum_score) return true;
  }
  return false;
}
} // namespace readability
