/**
 * @file CanAdapter_TestSupport_uTest.hpp
 * @brief Inline helpers shared by CAN adapter tests (no linking needed).
 */

#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_TEST_SUPPORT_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_TEST_SUPPORT_HPP

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace test_support {

/**
 * @brief Create an external PF_CAN/SOCK_RAW socket bound to @p interfaceName.
 * @return fd (>=0) on success; -1 on failure (and emits a gtest failure).
 */
inline int createTestCANSocket(const std::string& interfaceName) {
  int sock = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock < 0) {
    ADD_FAILURE() << "external CAN socket open failed: " << std::strerror(errno);
    return -1;
  }

  struct ifreq ifr;
  std::memset(&ifr, 0, sizeof(ifr));
  std::strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);

  if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
    ADD_FAILURE() << "ioctl SIOCGIFINDEX failed: " << std::strerror(errno);
    ::close(sock);
    return -1;
  }

  struct sockaddr_can addr;
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ADD_FAILURE() << "bind failed for external CAN socket: " << std::strerror(errno);
    ::close(sock);
    return -1;
  }
  return sock;
}

/**
 * @brief Return a stable test interface name.
 * @note If you need parallelism later, append a pid or counter here.
 */
inline std::string testIfName() { return "vcan0"; }

} // namespace test_support

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_TEST_SUPPORT_HPP
