/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
// This file is a C++ port of Mozilla's JSDOMParser.js.
#pragma once

#include "readability/dom.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace readability {

class LightweightDomParser {
public:
  LightweightDomParser();
  ~LightweightDomParser();
  LightweightDomParser(LightweightDomParser&&) noexcept;
  LightweightDomParser& operator=(LightweightDomParser&&) noexcept;
  LightweightDomParser(const LightweightDomParser&) = delete;
  LightweightDomParser& operator=(const LightweightDomParser&) = delete;

  [[nodiscard]] std::unique_ptr<dom::Document> parse(
      std::string_view html, std::string_view url = {});
  [[nodiscard]] const std::string& error_state() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace readability
