// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/main/LICENSE.

#pragma once

#include "caf/byte_buffer.hpp"
#include "caf/byte_span.hpp"
#include "caf/detail/core_export.hpp"
#include "caf/span.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace caf::detail {

class CAF_CORE_EXPORT base64 {
public:
  static void encode(std::string_view str, std::string& out);

  static void encode(std::string_view str, byte_buffer& out);

  static void encode(const_byte_span bytes, std::string& out);

  static void encode(const_byte_span bytes, byte_buffer& out);

  static std::string encode(std::string_view str) {
    std::string result;
    encode(str, result);
    return result;
  }

  static std::string encode(const_byte_span bytes) {
    std::string result;
    encode(bytes, result);
    return result;
  }

  static bool decode(std::string_view in, std::string& out);

  static bool decode(std::string_view in, byte_buffer& out);

  static bool decode(const_byte_span bytes, std::string& out);

  static bool decode(const_byte_span bytes, byte_buffer& out);

  static std::optional<std::string> decode(std::string_view in) {
    std::string result;
    if (decode(in, result))
      return {std::move(result)};
    else
      return {};
  }

  static std::optional<std::string> decode(const_byte_span in) {
    std::string result;
    if (decode(in, result))
      return {std::move(result)};
    else
      return {};
  }
};

} // namespace caf::detail
