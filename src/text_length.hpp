#pragma once

#include <cstddef>
#include <string_view>

namespace readability::detail {

// JavaScript String.length counts UTF-16 code units. The DOM abstraction uses
// UTF-8 strings, so every length which influences the upstream algorithm must
// be converted instead of using std::string::size() (a byte count).
inline std::size_t javascript_string_length(std::string_view value) noexcept {
  std::size_t units = 0;
  for (std::size_t index = 0; index < value.size();) {
    const auto first = static_cast<unsigned char>(value[index]);
    std::size_t width = 1;
    unsigned int code_point = first;
    if ((first & 0xe0U) == 0xc0U && index + 1U < value.size()) {
      width = 2;
      code_point = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U && index + 2U < value.size()) {
      width = 3;
      code_point = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U && index + 3U < value.size()) {
      width = 4;
      code_point = first & 0x07U;
    }

    bool valid = width > 1U;
    for (std::size_t offset = 1; valid && offset < width; ++offset) {
      const auto continuation = static_cast<unsigned char>(value[index + offset]);
      valid = (continuation & 0xc0U) == 0x80U;
      code_point = (code_point << 6U) | (continuation & 0x3fU);
    }
    if (!valid) {
      width = 1;
      code_point = first;
    }

    units += code_point > 0xffffU ? 2U : 1U;
    index += width;
  }
  return units;
}

} // namespace readability::detail
