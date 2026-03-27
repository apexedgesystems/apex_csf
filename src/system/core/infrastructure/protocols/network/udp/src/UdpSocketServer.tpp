/**
 * @file UdpSocketServer.tpp
 * @brief UDP socket server template method implementations.
 */

#ifndef APEX_PROTOCOLS_UDP_SOCKET_SERVER_TPP
#define APEX_PROTOCOLS_UDP_SOCKET_SERVER_TPP

#include "src/utilities/helpers/inc/Net.hpp" // isWouldBlock
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace udp {

template <size_t S>
typename UdpSocketServer::RecvInfo
UdpSocketServer::read(std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
                      std::optional<std::reference_wrapper<std::string>> error) {
  RecvInfo info{};
  info.addrLen = static_cast<socklen_t>(sizeof(info.addr));

  if (S == 0) {
    info.nread = 0;
    return info;
  }

  const ssize_t N = ::recvfrom(sockfd_.get(), bytes.data(), S, 0,
                               reinterpret_cast<struct sockaddr*>(&info.addr), &info.addrLen);
  if (N >= 0) {
    info.nread = N;
    return info;
  }

  if (apex::helpers::net::isWouldBlock(errno)) {
    info.nread = 0; // would block: no data available now
    return info;
  }

  // Hard error
  if (error) {
    error->get() = std::string("recvfrom failed: ") + std::strerror(errno);
  }
  info.nread = -1;
  return info;
}

} // namespace udp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_UDP_SOCKET_SERVER_TPP
