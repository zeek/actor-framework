/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
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

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

#include "caf/actor_clock.hpp"
#include "caf/actor_control_block.hpp"
#include "caf/group.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/message.hpp"
#include "caf/message_id.hpp"
#include "caf/variant.hpp"

namespace caf {
namespace detail {

class simple_actor_clock : public actor_clock {
public:
  // -- member types -----------------------------------------------------------

  /// Request for a `timeout_msg`.
  struct ordinary_timeout {
    strong_actor_ptr self;
    atom_value type;
    uint64_t id;
  };

  /// Request for a `sec::request_timeout` error.
  struct request_timeout {
    strong_actor_ptr self;
    message_id id;
  };

  /// Request for sending a message to an actor at a later time.
  struct actor_msg {
    strong_actor_ptr receiver;
    mailbox_element_ptr content;
  };

  /// Request for sending a message to a group at a later time.
  struct group_msg {
    group target;
    strong_actor_ptr sender;
    message content;
  };

  using value_type = variant<ordinary_timeout, request_timeout,
                             actor_msg, group_msg>;

  using map_type = std::multimap<time_point, value_type>;

  using secondary_map = std::multimap<abstract_actor*, map_type::iterator>;

  struct ordinary_predicate {
    using msg_type = ordinary_timeout;
    using arg_type = atom_value;
    arg_type type;
    bool operator()(const secondary_map::value_type& x) const noexcept;
  };

  struct request_predicate {
    using msg_type = request_timeout;
    using arg_type = message_id;
    arg_type id;
    bool operator()(const secondary_map::value_type& x) const noexcept;
  };

  struct visitor {
    simple_actor_clock* thisptr;

    void operator()(ordinary_timeout& x);

    void operator()(request_timeout& x);

    void operator()(actor_msg& x);

    void operator()(group_msg& x);
  };

  // -- constructors and destructors -------------------------------------------

  simple_actor_clock();

  void set_ordinary_timeout(time_point t, abstract_actor* self,
                           atom_value type, uint64_t id) final;

  void set_request_timeout(time_point t, abstract_actor* self,
                           message_id id) final;

  void cancel_ordinary_timeout(abstract_actor* self, atom_value type) final;

  void cancel_request_timeout(abstract_actor* self, message_id id) final;

  void cancel_timeouts(abstract_actor* self) final;

  void schedule_message(time_point t, strong_actor_ptr receiver,
                        mailbox_element_ptr content) final;

  void schedule_message(time_point t, group target, strong_actor_ptr sender,
                        message content) final;

  void cancel_all() final;

  void run_dispatch_loop();

  void cancel_dispatch_loop();

  const map_type& schedule() const {
    return schedule_;
  }

  const secondary_map& actor_lookup() const {
    return actor_lookup_;
  }

protected:
  template <class Predicate>
  secondary_map::iterator lookup(abstract_actor* self,
                                 Predicate pred) {
    auto e = actor_lookup_.end();
    auto range = actor_lookup_.equal_range(self);
    if (range.first == range.second)
      return e;
    auto i = std::find_if(range.first, range.second, pred);
    return i != range.second ? i : e;
  }

  template <class Predicate>
  void cancel(abstract_actor* self, typename Predicate::arg_type arg) {
    // Makes sure we don't release actor references while holding the mutex.
    typename Predicate::msg_type old;
    Predicate pred{arg};
    critical_section([&] {
      if (done_)
        return;
      auto i = lookup(self, pred);
      if (i != actor_lookup_.end()) {
        old = std::move(get<typename Predicate::msg_type>(i->second->second));
        schedule_.erase(i->second);
        actor_lookup_.erase(i);
      }
      cv_.notify_all();
    });
  }

  template <class Predicate>
  void update(time_point new_timeout, abstract_actor* self,
              typename Predicate::arg_type arg,
              typename Predicate::msg_type&& new_entry){
    // Makes sure we don't release actor references while holding the mutex.
    typename Predicate::msg_type old;
    Predicate pred{arg};
    critical_section([&] {
      if (done_)
        return;
      auto i = lookup(self, pred);
      if (i != actor_lookup_.end()) {
        old = std::move(get<typename Predicate::msg_type>(i->second->second));
        schedule_.erase(i->second);
        i->second = schedule_.emplace(new_timeout, std::move(new_entry));
      } else {
        auto j = schedule_.emplace(new_timeout, std::move(new_entry));
        actor_lookup_.emplace(self, j);
      }
      cv_.notify_all();
    });
  }

  template <class Predicate>
  void drop_lookup(abstract_actor* self, Predicate pred) {
    auto i = lookup(self, pred);
    if (i != actor_lookup_.end())
      actor_lookup_.erase(i);
  }

  /// Removes an element from the schedule, also removing it from actor_lookup
  /// if necessary.
  map_type::iterator erase_schedule_element(map_type::iterator i);

  /// Timeout schedule.
  map_type schedule_;

  /// Secondary index for accessing timeouts by actor.
  secondary_map actor_lookup_;

private:
  template <class F>
  void critical_section(F f) {
    std::unique_lock<std::mutex> guard{mx_};
    f();
  }

  bool fetch_next(std::vector<value_type>& timed_out);

  /// Guards schedule_ and actor_lookup_.
  std::mutex mx_;

  /// Signals changes to schedule_.
  std::condition_variable cv_;

  /// Synchronizes the dispatch loop when calling cancel_dispatch_loop.
  std::atomic<bool> done_;
};

} // namespace detail
} // namespace caf

