// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/net/fwd.hpp"
#include "caf/net/ssl/context.hpp"
#include "caf/net/tcp_accept_socket.hpp"

#include "caf/detail/net_export.hpp"
#include "caf/expected.hpp"
#include "caf/logger.hpp"

#include <variant>

namespace caf::net::ssl {

/// Wraps a TCP accept socket and an SSL context.
class CAF_NET_EXPORT tcp_acceptor {
public:
  // -- member types -----------------------------------------------------------

  using transport_type = transport;

  // -- constructors, destructors, and assignment operators --------------------

  tcp_acceptor() = delete;

  tcp_acceptor(const tcp_acceptor&) = delete;

  tcp_acceptor& operator=(const tcp_acceptor&) = delete;

  tcp_acceptor(tcp_acceptor&& other);

  tcp_acceptor& operator=(tcp_acceptor&& other);

  tcp_acceptor(tcp_accept_socket fd, context ctx)
    : fd_(fd), ctx_(std::move(ctx)) {
    // nop
  }

  // -- factories --------------------------------------------------------------

  static expected<tcp_acceptor>
  make_with_cert_file(tcp_accept_socket fd, const char* cert_file_path,
                      const char* key_file_path,
                      format file_format = format::pem);

  static expected<tcp_acceptor>
  make_with_cert_file(uint16_t port, const char* cert_file_path,
                      const char* key_file_path,
                      format file_format = format::pem);

  static expected<tcp_acceptor>
  make_with_cert_file(tcp_accept_socket fd, const std::string& cert_file_path,
                      const std::string& key_file_path,
                      format file_format = format::pem) {
    return make_with_cert_file(fd, cert_file_path.c_str(),
                               key_file_path.c_str(), file_format);
  }

  static expected<tcp_acceptor>
  make_with_cert_file(uint16_t port, const std::string& cert_file_path,
                      const std::string& key_file_path,
                      format file_format = format::pem) {
    return make_with_cert_file(port, cert_file_path.c_str(),
                               key_file_path.c_str(), file_format);
  }

  // -- properties -------------------------------------------------------------

  tcp_accept_socket fd() const noexcept {
    return fd_;
  }

  context& ctx() noexcept {
    return ctx_;
  }

  const context& ctx() const noexcept {
    return ctx_;
  }

private:
  tcp_accept_socket fd_;
  context ctx_;
};

// -- free functions -----------------------------------------------------------

/// Checks whether `acc` has a valid socket descriptor.
bool CAF_NET_EXPORT valid(const tcp_acceptor& acc);

/// Closes the socket of `obj`.
void CAF_NET_EXPORT close(tcp_acceptor& acc);

/// Tries to accept a new connection on `acc`. On success, wraps the new socket
/// into an SSL @ref connection and returns it.
expected<connection> CAF_NET_EXPORT accept(tcp_acceptor& acc);

/// Returns a function that, when called with an accept socket, calls `f`
/// either with a new SSL acceptor from `ctx` or with the file the file
/// descriptor if no SSL context is defined.
template <class F>
auto acceptor_with_ctx(std::shared_ptr<ssl::context> ctx, F&& f) {
  return [ctx, g = std::forward<F>(f)](auto fd) mutable {
    if (ctx) {
      auto acc = ssl::tcp_acceptor{fd, std::move(*ctx)};
      return g(acc);
    }
    return g(fd);
  };
}

} // namespace caf::net::ssl
