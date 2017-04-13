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

#include "caf/config.hpp"

#define CAF_SUITE io_typed_broker
#include "caf/test/io_dsl.hpp"

#include <memory>
#include <iostream>

#include "caf/all.hpp"
#include "caf/io/all.hpp"

#include "caf/string_algorithms.hpp"

using namespace std;
using namespace caf;
using namespace caf::io;

namespace {

using publish_atom = atom_constant<atom("publish")>;
using ping_atom = caf::atom_constant<atom("ping")>;
using pong_atom = caf::atom_constant<atom("pong")>;
using kickoff_atom = caf::atom_constant<atom("kickoff")>;

using peer = connection_handler::extend<reacts_to<ping_atom, int>,
                                        reacts_to<pong_atom, int>>;

using acceptor = accept_handler::extend<replies_to<publish_atom>::with<uint16_t>>;

using ping_actor = typed_actor<replies_to<pong_atom, int>::with<ping_atom, int>>;

using pong_actor = typed_actor<replies_to<ping_atom, int>::with<pong_atom, int>>;

behavior ping(event_based_actor* self, size_t num_pongs) {
  CAF_MESSAGE("num_pongs: " << num_pongs);
  auto count = std::make_shared<size_t>(0);
  return {
    [=](kickoff_atom, const peer& pong) {
      CAF_MESSAGE("received `kickoff_atom`");
      self->send(pong, ping_atom::value, 1);
      self->become(
        [=](pong_atom, int value) -> optional<std::tuple<ping_atom, int>> {
          if (++*count >= num_pongs) {
            CAF_MESSAGE("received " << num_pongs
                        << " pongs, call self->quit");
            self->quit();
            return none;
          }
          return std::make_tuple(ping_atom::value, value + 1);
        }
      );
    }
  };
}

behavior pong(event_based_actor* self) {
  CAF_MESSAGE("pong actor started");
  self->set_down_handler([=](down_msg& dm) {
    CAF_MESSAGE("received: " << to_string(dm.reason));
    self->quit(dm.reason);
  });
  return {
    [=](ping_atom, int value) -> std::tuple<atom_value, int> {
      CAF_MESSAGE("received: 'ping', " << value);
      self->monitor(self->current_sender());
      // set next behavior
      self->become(
        [](ping_atom, int val) {
          CAF_MESSAGE("received: 'ping', " << val);
          return std::make_tuple(pong_atom::value, val);
        }
      );
      // reply to 'ping'
      return std::make_tuple(pong_atom::value, value);
    }
  };
}

class peer_bhvr : public composable_behavior<peer> {
public:
  result<void> operator()(param<connection_closed_msg>) override {
    CAF_MESSAGE("received connection_closed_msg");
    self->quit();
    return unit;
  }

  result<void> operator()(param<new_data_msg> msg) override {
    CAF_MESSAGE("received new_data_msg");
    atom_value x;
    int y;
    binary_deserializer source{self->system(), msg->buf};
    auto e = source(x, y);
    CAF_REQUIRE(!e);
    if (x == pong_atom::value)
      self->send(actor_cast<ping_actor>(buddy_), pong_atom::value, y);
    else
      self->send(actor_cast<pong_actor>(buddy_), ping_atom::value, y);
    return unit;
  }

  result<void> operator()(ping_atom, int value) override {
    CAF_MESSAGE("received: 'ping', " << value);
    printf("%s %d - %s\n", __FILE__, __LINE__, deep_to_string(value).c_str());
    write(ping_atom::value, value);
    return unit;
  }

  result<void> operator()(pong_atom, int value) override {
    CAF_MESSAGE("received: 'pong', " << value);
    write(pong_atom::value, value);
    return unit;
  }

  void init(peer::broker_pointer selfptr, const actor& buddy,
            connection_handle hdl) {
    self_ = selfptr;
    buddy_ = buddy;
    hdl_ = hdl;
  }

private:
  void write(atom_value x, int y) {
    auto& buf = self_->wr_buf(hdl_);
    binary_serializer sink{self->system(), buf};
    auto e = sink(x, y);
    CAF_REQUIRE(!e);
    printf("%s %d - %s\n", __FILE__, __LINE__, deep_to_string(buf).c_str());
    self_->flush(hdl_);
  }

  peer::broker_pointer self_;
  actor buddy_;
  connection_handle hdl_;
};

peer::behavior_type
peer_bhvr_impl(composable_behavior_based_actor<peer_bhvr, peer_bhvr::broker_base>* self,
               connection_handle hdl, const actor& buddy) {
  CAF_MESSAGE("peer_fun called");
  self->monitor(buddy);
  self->state.init(self, buddy, hdl);
  // assume exactly one connection
  CAF_REQUIRE_EQUAL(self->connections().size(), 1u);
  self->configure_read(
    hdl, receive_policy::exactly(sizeof(atom_value) + sizeof(int)));
  self->set_down_handler([=](down_msg& dm) {
    CAF_MESSAGE("received down_msg");
    if (dm.source == buddy)
      self->quit(std::move(dm.reason));
  });
  return self->make_behavior();
}

peer::behavior_type peer_fun(peer::broker_pointer self, connection_handle hdl,
                             const actor& buddy) {
  CAF_MESSAGE("peer_fun called");
  self->monitor(buddy);
  // Assume exactly one connection.
  CAF_REQUIRE_EQUAL(self->connections().size(), 1u);
  constexpr size_t msg_size = sizeof(atom_value) + sizeof(int);
  self->configure_read(hdl, receive_policy::exactly(msg_size));
  auto write = [=](atom_value x, int y) {
    auto& buf = self->wr_buf(hdl);
    binary_serializer sink{self->system(), buf};
    auto err = sink(x, y);
    CAF_REQUIRE(!err);
    self->flush(hdl);
  };
  self->set_down_handler([=](down_msg& dm) {
    CAF_MESSAGE("broker received down_msg");
    if (dm.source == buddy)
      self->quit(std::move(dm.reason));
  });
  return {
    [=](const connection_closed_msg&) {
      CAF_MESSAGE("broker received connection_closed_msg");
      self->quit();
    },
    //[=](const new_data_msg& msg) {
    [=](param<new_data_msg> msg) -> result<void> {
      CAF_MESSAGE("broker received new_data_msg");
      atom_value x;
      int y;
      binary_deserializer source{self->system(), msg->buf};
      auto e = source(x, y);
      CAF_REQUIRE(!e);
      if (x == pong_atom::value)
        self->send(actor_cast<ping_actor>(buddy), pong_atom::value, y);
      else
        self->send(actor_cast<pong_actor>(buddy), ping_atom::value, y);
      return unit;
    },
    [=](ping_atom, int value) {
      CAF_MESSAGE("broker received ('ping', " << value << ")");
      write(ping_atom::value, value);
    },
    [=](pong_atom, int value) {
      CAF_MESSAGE("broker received ('pong', " << value << ")");
      write(pong_atom::value, value);
    }
  };
}

acceptor::behavior_type acceptor_fun(acceptor::broker_pointer self,
                                     const actor& buddy) {
  CAF_MESSAGE("peer_acceptor_fun");
  return {
    [=](param<new_connection_msg> msg) {
      CAF_MESSAGE("received `new_connection_msg`");
      self->fork(peer_fun, msg->handle, buddy);
      //self->fork(peer_bhvr_impl, msg->handle, buddy);
      self->quit();
    },
    [](const acceptor_closed_msg&) {
      // nop
    },
    [=](publish_atom) -> expected<uint16_t> {
      auto dm = self->add_tcp_doorman(0, "127.0.0.1");
      if (dm)
        return get<1>(*dm);
      return std::move(dm.error());
    }
  };
}

void run_client(int argc, char** argv, uint16_t port) {
  actor_system_config cfg;
  actor_system system{cfg.load<io::middleman>().parse(argc, argv)};
  auto p = system.spawn(ping, size_t{10});
  CAF_MESSAGE("spawn_client_typed...");
  CAF_EXP_THROW(cl, system.middleman().spawn_client(peer_fun, "localhost",
                                                    port, p));
  CAF_MESSAGE("spawn_client_typed finished");
  anon_send(p, kickoff_atom::value, cl);
  CAF_MESSAGE("`kickoff_atom` has been send");
}

void run_server(int argc, char** argv) {
  actor_system_config cfg;
  actor_system system{cfg.load<io::middleman>().parse(argc, argv)};
  scoped_actor self{system};
  auto serv = system.middleman().spawn_broker(acceptor_fun, system.spawn(pong));
  std::thread child;
  self->request(serv, infinite, publish_atom::value).receive(
    [&](uint16_t port) {
      CAF_MESSAGE("server is running on port " << port);
      child = std::thread([=] {
        run_client(argc, argv, port);
      });
    },
    [&](error& err) {
      CAF_FAIL("error: " << system.render(err));
    }
  );
  self->await_all_other_actors_done();
  CAF_MESSAGE("wait for client system");
  child.join();
}

class remoting_config : public actor_system_config {
public:
  remoting_config() {
    load<middleman, network::test_multiplexer>();
    middleman_detach_utility_actors = false;
  }
};

template <class T>
T unwrap(expected<T> x) {
  if (!x)
    CAF_ERROR(to_string(x.error()));
  return std::move(*x);
}

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(typed_brokers, point_to_point_fixture<remoting_config>)

CAF_TEST(test_typed_broker) {
  constexpr const char* host = "earth";
  constexpr uint16_t port = 8080u;
  // Prepare connections.
  prepare_connection(earth, mars, host, port);
  // Spawn testees.
  auto earth_pong = earth.sys.spawn(pong);
  CAF_MESSAGE("spawn server on earth");
  auto earth_server = unwrap(earth.mm.spawn_server(acceptor_fun, port,
                                                   earth_pong));
  auto mars_ping = mars.sys.spawn(ping, size_t{2});
  CAF_MESSAGE("spawn client on mars");
  auto mars_client = unwrap(mars.mm.spawn_client(peer_fun, host,
                                                 port, mars_ping));
  // Run any initialization code.
  exec_all();
  earth.mpx.accept_connection(earth.acc);
  // Test sequence.
  anon_send(mars_ping, kickoff_atom::value, mars_client);
  expect_on(mars, (atom_value, peer),
            from(_).to(mars_ping).with(kickoff_atom::value, mars_client));
  network_traffic();
  expect_on(earth, (atom_value, int),
            from(_).to(earth_pong).with(ping_atom::value, 0));
  network_traffic();
  expect_on(mars, (atom_value, int),
            from(_).to(mars_ping).with(pong_atom::value, 0));
  network_traffic();
  expect_on(earth, (atom_value, int),
            from(_).to(earth_pong).with(ping_atom::value, 1));
  network_traffic();
  expect_on(mars, (atom_value, int),
            from(_).to(mars_ping).with(pong_atom::value, 1));
  network_traffic();
  expect_on(earth, (down_msg),
            from(_).to(earth_pong).with(_));
}

CAF_TEST_FIXTURE_SCOPE_END()
