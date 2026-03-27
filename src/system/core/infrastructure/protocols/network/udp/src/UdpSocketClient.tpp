/**
 * @file UdpSocketClient.tpp
 * @brief UDP socket client template method implementations.
 */

#ifndef APEX_PROTOCOLS_UDP_SOCKET_CLIENT_TPP
#define APEX_PROTOCOLS_UDP_SOCKET_CLIENT_TPP

#include "src/utilities/helpers/inc/Net.hpp" // isWouldBlock
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketClient.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace udp {

template <size_t S>
ssize_t UdpSocketClient::read(std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
                              std::optional<std::reference_wrapper<std::string>> error) {
  if (S == 0)
    return 0;

  const ssize_t N = ::recv(sockfd_.get(), bytes.data(), S, 0);
  if (N >= 0)
    return N;

  if (apex::helpers::net::isWouldBlock(errno))
    return 0;

  if (error)
    error->get() = std::string("recv failed: ") + std::strerror(errno);
  return -1;
}

template <size_t S>
ssize_t UdpSocketClient::readFrom(std::array<uint8_t, S>& bytes, PeerInfo& from,
                                  int /*timeoutMilliSecs*/,
                                  std::optional<std::reference_wrapper<std::string>> error) {
  if (S == 0)
    return 0;

  from.addrLen = static_cast<socklen_t>(sizeof(from.addr));
  const ssize_t N = ::recvfrom(sockfd_.get(), bytes.data(), S, 0,
                               reinterpret_cast<struct sockaddr*>(&from.addr), &from.addrLen);
  if (N >= 0)
    return N;

  if (apex::helpers::net::isWouldBlock(errno))
    return 0;

  if (error)
    error->get() = std::string("recvfrom failed: ") + std::strerror(errno);
  return -1;
}

} // namespace udp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_UDP_SOCKET_CLIENT_TPP
