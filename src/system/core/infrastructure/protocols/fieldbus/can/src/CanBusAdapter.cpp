/**
 * @file CanBusAdapter.cpp
 * @brief Linux SocketCAN adapter implementation.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

using apex::protocols::TraceDirection;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

// Map common errno to Status.
inline Status mapReadWriteErrnoToStatus(int err) {
  if (err == EAGAIN || err == EWOULDBLOCK)
    return Status::WOULD_BLOCK;
  if (err == ETIMEDOUT)
    return Status::ERROR_TIMEOUT;
  if (err == EBADF)
    return Status::ERROR_CLOSED;
  return Status::ERROR_IO;
}

// Translate Linux CAN ID bits into CanId.
inline void fillCanIdFromLinux(uint32_t canId, CanId& out) {
  out.extended = (canId & CAN_EFF_FLAG) != 0;
  out.remote = (canId & CAN_RTR_FLAG) != 0;
  out.error = (canId & CAN_ERR_FLAG) != 0;
  out.id = out.extended ? (canId & CAN_EFF_MASK) : (canId & CAN_SFF_MASK);
}

// Build Linux can_id from CanId.
inline uint32_t buildLinuxCanId(const CanId& in) {
  uint32_t id = in.extended ? ((in.id & CAN_EFF_MASK) | CAN_EFF_FLAG) : (in.id & CAN_SFF_MASK);
  if (in.remote)
    id |= CAN_RTR_FLAG;
  if (in.error)
    id |= CAN_ERR_FLAG;
  return id;
}

} // namespace

/* -------------------------- CANBusAdapter Methods ------------------------- */

CANBusAdapter::CANBusAdapter(std::string description, std::string interfaceName)
    : desc_(std::move(description)), ifName_(std::move(interfaceName)) {}

CANBusAdapter::~CANBusAdapter() { closeSocket(); }

void CANBusAdapter::closeSocket() noexcept {
  if (sockfd_ >= 0) {
    ::close(sockfd_);
    sockfd_ = -1;
  }
}

// Open PF_CAN/SOCK_RAW and bind to interface ifName_ (idempotent).
Status CANBusAdapter::openAndBindIfNeeded() {
  if (sockfd_ >= 0)
    return Status::SUCCESS;

  // Prefer atomic CLOEXEC + NONBLOCK; fall back for older kernels/libc.
  int s = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
  if (s < 0 && errno == EINVAL) {
    s = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0)
      return Status::ERROR_IO;

    // NONBLOCK
    int fl = ::fcntl(s, F_GETFL, 0);
    if (fl < 0 || ::fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) {
      ::close(s);
      return Status::ERROR_IO;
    }
    // CLOEXEC
    int fdfl = ::fcntl(s, F_GETFD, 0);
    if (fdfl >= 0)
      (void)::fcntl(s, F_SETFD, fdfl | FD_CLOEXEC);
  } else if (s < 0) {
    return Status::ERROR_IO;
  }

  // Resolve interface index
  ifreq ifr{};
  std::memset(&ifr, 0, sizeof(ifr));
  std::strncpy(ifr.ifr_name, ifName_.c_str(), IFNAMSIZ - 1);

  if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
    ::close(s);
    return Status::ERROR_IO;
  }

  // Bind
  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(s);
    return Status::ERROR_IO;
  }

  sockfd_ = s;
  return Status::SUCCESS;
}

// Apply acceptance filters (leave kernel default = accept-all when empty).
Status CANBusAdapter::applyFilters(const CanConfig& cfg) {
  if (cfg.filters.empty())
    return Status::SUCCESS;

  std::vector<can_filter> flt;
  flt.reserve(cfg.filters.size());
  for (const auto& f : cfg.filters) {
    can_filter c{};
    if (f.extended) {
      c.can_id = (f.id & CAN_EFF_MASK) | CAN_EFF_FLAG;
      c.can_mask = (f.mask & CAN_EFF_MASK) | CAN_EFF_FLAG;
    } else {
      c.can_id = (f.id & CAN_SFF_MASK);
      c.can_mask = (f.mask & CAN_SFF_MASK);
    }
    flt.push_back(c);
  }

  if (::setsockopt(sockfd_, SOL_CAN_RAW, CAN_RAW_FILTER, flt.data(),
                   static_cast<socklen_t>(flt.size() * sizeof(can_filter))) < 0) {
    return Status::ERROR_IO;
  }
  return Status::SUCCESS;
}

// Toggle kernel loopback.
Status CANBusAdapter::setLoopback(bool enable) {
  const int LOOP = enable ? 1 : 0;
  if (::setsockopt(sockfd_, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &LOOP, sizeof(LOOP)) < 0) {
    return Status::ERROR_IO;
  }
  return Status::SUCCESS;
}

// poll()-based wait. timeoutMs: <0 = block, 0 = no-wait, >0 = ms
int CANBusAdapter::waitFd(int fd, short events, int timeoutMs) noexcept {
  if (timeoutMs == 0)
    return 0;

  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = events;

  const int TMO = (timeoutMs < 0) ? -1 : timeoutMs;

  int rv;
  do {
    rv = ::poll(&pfd, 1, TMO);
  } while (rv < 0 && errno == EINTR);

  if (rv > 0) {
    // Surface hard fd errors uniformly (treat as closed/I/O error).
    if (pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) {
      errno = EBADF; // map through our errno->Status helper
      return -1;
    }
    // Rare: ready without requested events (spurious wakeup); treat as I/O error.
    if ((pfd.revents & events) == 0) {
      errno = EIO;
      return -1;
    }
  }
  return rv; // 0 = timeout; >0 ready; <0 error (errno set)
}

// Configure: open/bind, set loopback & filters. Bitrate/listenOnly not applied here.
Status CANBusAdapter::configure(const CanConfig& cfg) {
  if (auto st = openAndBindIfNeeded(); st != Status::SUCCESS)
    return st;

  if (auto st2 = setLoopback(cfg.loopback); st2 != Status::SUCCESS)
    return st2;
  if (auto st3 = applyFilters(cfg); st3 != Status::SUCCESS)
    return st3;

  lastCfg_ = cfg; // accepted (even if some fields ignored by backend)
  configured_ = true;
  return Status::SUCCESS;
}

// Receive one frame with fast path: try read first; poll only if needed.
Status CANBusAdapter::recv(CanFrame& outFrame, int timeoutMs) {
  if (!configured_)
    return Status::ERROR_NOT_CONFIGURED;
  if (sockfd_ < 0)
    return Status::ERROR_CLOSED;

  for (;;) {
    can_frame frame{};
    ssize_t n = ::read(sockfd_, &frame, sizeof(frame));
    if (n == static_cast<ssize_t>(sizeof(frame))) {
      // Translate to CanFrame
      fillCanIdFromLinux(frame.can_id, outFrame.canId);
      outFrame.dlc = (frame.can_dlc <= 8) ? frame.can_dlc : 8; // defensive cap
      if (outFrame.dlc) {
        std::memcpy(outFrame.data.data(), frame.data, outFrame.dlc);
      }
      outFrame.timestampNs.reset(); // SO_TIMESTAMPING not enabled by default

      // Update statistics
      ++stats_.framesReceived;
      stats_.bytesReceived += outFrame.dlc;
      if (outFrame.canId.error) {
        ++stats_.errorFrames;
      }
      invokeTrace(TraceDirection::RX, outFrame.data.data(), outFrame.dlc);
      return Status::SUCCESS;
    }

    if (n < 0) {
      const int ERR = errno;
      if (ERR == EINTR)
        continue; // try again
      if (ERR == EAGAIN || ERR == EWOULDBLOCK) {
        int wrv = waitFd(sockfd_, POLLIN, timeoutMs);
        if (wrv == 0) {
          ++stats_.recvWouldBlock;
          return Status::WOULD_BLOCK; // timeout/nonblocking
        }
        if (wrv < 0) {
          ++stats_.recvErrors;
          return mapReadWriteErrnoToStatus(errno);
        }
        continue; // ready -> retry read
      }
      ++stats_.recvErrors;
      return mapReadWriteErrnoToStatus(ERR);
    }

    // Short read (unexpected with raw CAN).
    ++stats_.recvErrors;
    return Status::ERROR_IO;
  }
}

// Batch receive: drain all available frames (up to maxFrames).
std::size_t CANBusAdapter::recvBatch(CanFrame* frames, std::size_t maxFrames, int timeoutMs) {
  if (!configured_ || sockfd_ < 0 || frames == nullptr || maxFrames == 0)
    return 0;

  std::size_t count = 0;

  // First frame uses the provided timeout
  Status st = recv(frames[0], timeoutMs);
  if (st == Status::SUCCESS) {
    ++count;
  } else {
    // No frame available within timeout (or error) - return 0
    return 0;
  }

  // Remaining frames: nonblocking drain
  while (count < maxFrames) {
    st = recv(frames[count], 0); // timeoutMs=0 = nonblocking
    if (st == Status::SUCCESS) {
      ++count;
    } else {
      // No more frames available or error - stop draining
      break;
    }
  }

  return count;
}

// Send one frame with fast path: try write first; poll only if needed.
Status CANBusAdapter::send(const CanFrame& frame, int timeoutMs) {
  if (!configured_)
    return Status::ERROR_NOT_CONFIGURED;
  if (sockfd_ < 0)
    return Status::ERROR_CLOSED;
  if (frame.dlc > 8)
    return Status::ERROR_INVALID_ARG;

  can_frame out{};
  out.can_id = buildLinuxCanId(frame.canId);
  out.can_dlc = frame.dlc;
  if (frame.dlc) {
    std::memcpy(out.data, frame.data.data(), frame.dlc);
  }

  for (;;) {
    ssize_t n = ::write(sockfd_, &out, sizeof(out));
    if (n == static_cast<ssize_t>(sizeof(out))) {
      // Update statistics
      ++stats_.framesSent;
      stats_.bytesTransmitted += frame.dlc;
      invokeTrace(TraceDirection::TX, frame.data.data(), frame.dlc);
      return Status::SUCCESS;
    }

    if (n < 0) {
      const int ERR = errno;
      if (ERR == EINTR)
        continue;
      if (ERR == EAGAIN || ERR == EWOULDBLOCK) {
        int wrv = waitFd(sockfd_, POLLOUT, timeoutMs);
        if (wrv == 0) {
          ++stats_.sendWouldBlock;
          return Status::WOULD_BLOCK; // timeout/nonblocking
        }
        if (wrv < 0) {
          ++stats_.sendErrors;
          return mapReadWriteErrnoToStatus(errno);
        }
        continue; // ready -> retry write
      }
      ++stats_.sendErrors;
      return mapReadWriteErrnoToStatus(ERR);
    }

    // Short write (unexpected with raw CAN).
    ++stats_.sendErrors;
    return Status::ERROR_IO;
  }
}

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex
