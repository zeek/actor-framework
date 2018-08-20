/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <chrono>
#include <initializer_list>

#include "caf/config.hpp"
#include "caf/timespan.hpp"
#include "caf/timestamp.hpp"

namespace caf {
namespace detail {

/// Converts realtime into a series of ticks, whereas each tick represents a
/// preconfigured timespan. For example, a tick emitter configured with a
/// timespan of 25ms generates a tick every 25ms after starting it.
class tick_emitter {
public:
  // -- constructors, destructors, and assignment operators --------------------

  tick_emitter();

  tick_emitter(timestamp now);

  /// Queries whether the start time is non-default constructed.
  bool started() const;

  /// Configures the start time.
  void start(timestamp now);

  /// Resets the start time to 0.
  void stop();

  /// Configures the time interval per tick.
  void interval(timespan x);

  /// Returns the time interval per tick.
  inline timespan interval() const {
    return interval_;
  }

  /// Advances time and calls `consumer` for each emitted tick.
  template <class F>
  void update(timestamp now, F& consumer) {
    CAF_ASSERT(interval_.count() != 0);
    auto d = now - start_;
    auto current_tick_id = static_cast<size_t>(d.count() / interval_.count());
    while (last_tick_id_ < current_tick_id)
      consumer(++last_tick_id_);
  }

  /// Advances time by `t` and returns all triggered periods as bitmask.
  size_t timeouts(timestamp t, std::initializer_list<size_t> periods);

  /// Returns the next time point after `t` that would trigger any of the tick
  /// periods, i.e., returns the next time where any of the tick periods
  /// triggers `tick id % period == 0`.
  timestamp next_timeout(timestamp t, std::initializer_list<size_t> periods);

private:
  timestamp start_;
  timespan interval_;
  size_t last_tick_id_;
};

} // namespace detail
} // namespace caf


