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

#include "caf/detail/simple_actor_clock.hpp"

#include "caf/actor_cast.hpp"
#include "caf/sec.hpp"
#include "caf/system_messages.hpp"

namespace {

using guard_type = std::unique_lock<std::mutex>;

} // namespace <anonymous>

namespace caf {
namespace detail {

bool simple_actor_clock::ordinary_predicate::
operator()(const secondary_map::value_type& x) const noexcept {
  auto ptr = get_if<ordinary_timeout>(&x.second->second);
  return ptr != nullptr ? ptr->type == type : false;
}

bool simple_actor_clock::request_predicate::
operator()(const secondary_map::value_type& x) const noexcept {
  auto ptr = get_if<request_timeout>(&x.second->second);
  return ptr != nullptr ? ptr->id == id : false;
}

void simple_actor_clock::visitor::operator()(ordinary_timeout& x) {
  CAF_ASSERT(x.self != nullptr);
  x.self->get()->eq_impl(make_message_id(), x.self, nullptr,
                         timeout_msg{x.type, x.id});
}

void simple_actor_clock::visitor::operator()(request_timeout& x) {
  CAF_ASSERT(x.self != nullptr);
  x.self->get()->eq_impl(x.id, x.self, nullptr, sec::request_timeout);
}

void simple_actor_clock::visitor::operator()(actor_msg& x) {
  x.receiver->enqueue(std::move(x.content), nullptr);
}

void simple_actor_clock::visitor::operator()(group_msg& x) {
  x.target->eq_impl(make_message_id(), std::move(x.sender), nullptr,
                    std::move(x.content));
}

simple_actor_clock::simple_actor_clock() : done_(false) {
  // nop
}

void simple_actor_clock::set_ordinary_timeout(time_point t, abstract_actor* self,
                                             atom_value type, uint64_t id) {
  ordinary_timeout tmp{actor_cast<strong_actor_ptr>(self), type, id};
  update<ordinary_predicate>(t, self, type, std::move(tmp));
}

void simple_actor_clock::set_request_timeout(time_point t, abstract_actor* self,
                                             message_id id) {
  request_timeout tmp{actor_cast<strong_actor_ptr>(self), id};
  update<request_predicate>(t, self, id, std::move(tmp));
}

void simple_actor_clock::cancel_ordinary_timeout(abstract_actor* self,
                                                 atom_value type) {
  cancel<ordinary_predicate>(self, type);
}

void simple_actor_clock::cancel_request_timeout(abstract_actor* self,
                                                message_id id) {
  cancel<request_predicate>(self, id);
}

void simple_actor_clock::cancel_timeouts(abstract_actor* self) {
  // Makes sure we don't release actor references while holding the mutex.
  std::vector<value_type> old_entries;
  critical_section([&] {
    auto range = actor_lookup_.equal_range(self);
    if (range.first == range.second)
      return;
    old_entries.reserve(std::distance(range.first, range.second));
    for (auto i = range.first; i != range.second; ++i) {
      old_entries.emplace_back(std::move(i->second->second));
      schedule_.erase(i->second);
    }
    actor_lookup_.erase(range.first, range.second);
  });
}

void simple_actor_clock::schedule_message(time_point t,
                                          strong_actor_ptr receiver,
                                          mailbox_element_ptr content) {
  critical_section([&] {
    if (done_)
      return;
    schedule_.emplace(t, actor_msg{std::move(receiver), std::move(content)});
    cv_.notify_all();
  });
}

void simple_actor_clock::schedule_message(time_point t, group target,
                                          strong_actor_ptr sender,
                                          message content) {
  critical_section([&] {
    if (done_)
      return;
    schedule_.emplace(
      t, group_msg{std::move(target), std::move(sender), std::move(content)});
    cv_.notify_all();
  });
}

void simple_actor_clock::cancel_all() {
  // Makes sure we don't release actor references while holding the mutex.
  map_type old_schedule;
  critical_section([&] {
    actor_lookup_.clear();
    schedule_.swap(old_schedule);
  });
}

void simple_actor_clock::run_dispatch_loop() {
  std::vector<value_type> timed_out;
  visitor f{this};
  while (fetch_next(timed_out)) {
    for (auto& x : timed_out)
      visit(f, x);
    timed_out.clear();
  }
}

void simple_actor_clock::cancel_dispatch_loop() {
  guard_type guard{mx_};
  done_ = true;
  cv_.notify_all();
}

bool simple_actor_clock::fetch_next(std::vector<value_type>& timed_out) {
  guard_type guard{mx_};
  // Check whether the loop was canceled.
  if (done_) {
    schedule_.clear();
    return false;
  }
  // Wait for non-empty schedule.
  if (schedule_.empty()) {
    cv_.wait(guard);
  } else {
    auto tout = schedule_.begin()->first;
    cv_.wait_until(guard, tout);
  }
  // Double-check whether schedule is non-empty and execute it.
  if (!schedule_.empty()) {
    auto t = now();
    auto i = schedule_.begin();
    while (i != schedule_.end() && i->first <= t) {
      timed_out.emplace_back(std::move(i->second));
      i = erase_schedule_element(i);
    }
  }
  return true;
}

simple_actor_clock::map_type::iterator
simple_actor_clock::erase_schedule_element(map_type::iterator i) {
  // Check whether i is 'referenced' in actor_lookup.
  abstract_actor* self;
  if (auto ot = get_if<ordinary_timeout>(&i->second)) {
    self = ot->self->get();
  } else if (auto rt = get_if<request_timeout>(&i->second)) {
    self = rt->self->get();
  } else {
    // No reference to i gets stored for other types.
    return schedule_.erase(i);
  }
  // Remove from secondary map.
  auto range = actor_lookup_.equal_range(self);
  auto pred = [&](const secondary_map::value_type& kvp) {
    return kvp.second == i;
  };
  auto j = std::find_if(range.first, range.second, pred);
  if (j != range.second)
    actor_lookup_.erase(j);
  // Remove from schedule.
  return schedule_.erase(i);
}

} // namespace detail
} // namespace caf
