/**
 * @file TcpSocketServer.tpp
 * @brief TCP socket server template method implementations.
 */

#ifndef APEX_PROTOCOLS_TCP_SOCKET_SERVER_TPP
#define APEX_PROTOCOLS_TCP_SOCKET_SERVER_TPP

#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace tcp {

template <size_t S>
ssize_t TcpSocketServer::read(int clientfd, std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
                              std::optional<std::reference_wrapper<std::string>> error) {
  if (S == 0)
    return 0;

  size_t total = 0;
  for (;;) {
    ssize_t n = ::recv(clientfd, bytes.data() + total, S - total, 0);
    if (n > 0) {
      total += static_cast<size_t>(n);
      if (total == S)
        break; // buffer filled
      // Nonblocking drain: keep reading until EAGAIN or buffer full
      continue;
    }
    if (n == 0) {
      // Peer closed read-half; caller may choose to close(fd)
      break;
    }
    // n < 0
    if (errno == EAGAIN
#if defined(EWOULDBLOCK)
        || errno == EWOULDBLOCK
#endif
    ) {
      // Would block: nothing more available now
      break;
    }
    // Hard error
    if (error) {
      error->get() = std::string("recv failed from client: ") + std::strerror(errno);
    }
    return -1;
  }

  return static_cast<ssize_t>(total);
}

} // namespace tcp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_TCP_SOCKET_SERVER_TPP
