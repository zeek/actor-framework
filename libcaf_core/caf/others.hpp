// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/main/LICENSE.

#pragma once

#include "caf/catch_all.hpp"

namespace caf {

struct others_t {
  constexpr others_t() {
    // nop
  }

  template <class F>
  [[deprecated]] auto operator>>(F fun) const {
    return catch_all<F>{fun};
  }
};

constexpr others_t others = others_t{};

} // namespace caf
