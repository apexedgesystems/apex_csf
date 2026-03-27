/**
 * @file TcpSocketClient.tpp
 * @brief TCP socket client template method implementations.
 */

#ifndef APEX_PROTOCOLS_TCP_SOCKET_CLIENT_TPP
#define APEX_PROTOCOLS_TCP_SOCKET_CLIENT_TPP

#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"

#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace tcp {

template <size_t S>
ssize_t TcpSocketClient::read(std::array<uint8_t, S>& bytes, int timeoutMilliSecs,
                              std::optional<std::reference_wrapper<std::string>> error) {
  epoll_event event;
  int retval = epoll_wait(epfd_.get(), &event, 1, timeoutMilliSecs);
  if (retval == 0) {
    if (error)
      error->get() = "Read timeout: no data available within the specified timeout.";
    return 0;
  }
  if (retval < 0) {
    if (error)
      error->get() = std::string("epoll_wait failed: ") + std::strerror(errno);
    return -1;
  }

  // Handle peer shutdown (read-half or full close).
  if (event.events & (EPOLLERR | EPOLLHUP)) {
    // Let caller detect closure via recv==0; we don’t close here.
  }

  ssize_t nread = ::recv(sockfd_.get(), bytes.data(), bytes.size(), 0);
  if (nread < 0) {
    if (error)
      error->get() = std::string("recv failed: ") + std::strerror(errno);
    return -1;
  }
  // nread == 0 means orderly shutdown by peer.
  return nread;
}

} // namespace tcp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_TCP_SOCKET_CLIENT_TPP
