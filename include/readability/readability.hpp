// Readability++ public API.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "readability/dom.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace readability {

struct Article {
  std::string title;
  std::optional<std::string> byline;
  std::optional<std::string> dir;
  std::optional<std::string> lang;
  std::string content;
  std::string text_content;
  std::size_t length{};
  std::optional<std::string> excerpt;
  std::optional<std::string> site_name;
  std::optional<std::string> published_time;
};

struct Options {
  bool debug{false};
  std::size_t max_elements_to_parse{0};
  std::size_t top_candidates{5};
  std::size_t character_threshold{500};
  std::vector<std::string> classes_to_preserve{};
  bool keep_classes{false};
  bool disable_json_ld{false};
  double link_density_modifier{0.0};
  std::function<std::string(const dom::Node&)> serializer{};
  std::optional<std::regex> allowed_video_regex{};
};

struct ReaderableOptions {
  double minimum_score{20.0};
  std::size_t minimum_content_length{140};
  std::function<bool(const dom::Node&)> visibility_checker{};
};

class Readability {
public:
  explicit Readability(dom::Document& document, Options options = {});
  ~Readability();
  Readability(Readability&&) noexcept;
  Readability& operator=(Readability&&) noexcept;
  Readability(const Readability&) = delete;
  Readability& operator=(const Readability&) = delete;

  [[nodiscard]] std::optional<Article> parse();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool is_probably_readerable(
    const dom::Document& document, ReaderableOptions options = {});

} // namespace readability
