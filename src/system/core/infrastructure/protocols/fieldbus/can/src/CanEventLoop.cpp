/**
 * @file CanEventLoop.cpp
 * @brief Epoll-based CAN event loop implementation.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanEventLoop.hpp"

#include <algorithm>
#include <cerrno>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* -------------------------- CanEventLoop Methods -------------------------- */

CanEventLoop::~CanEventLoop() {
  if (epfd_ >= 0) {
    ::close(epfd_);
    epfd_ = -1;
  }
}

bool CanEventLoop::init() {
  if (epfd_ >= 0)
    return true; // Already initialized

  epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
  return epfd_ >= 0;
}

bool CanEventLoop::add(CANBusAdapter* adapter, CanEventCallback callback) {
  if (epfd_ < 0 || adapter == nullptr)
    return false;

  // Check capacity
  if (adapters_.size() >= MAX_DEVICES)
    return false;

  // Check not already registered
  auto it = findAdapter(adapter);
  if (it != adapters_.end())
    return false;

  // Get socket fd from adapter
  int fd = adapter->socketFd();
  if (fd < 0)
    return false;

  // Add to epoll
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET; // Edge-triggered for efficiency
  ev.data.ptr = adapter;         // Store adapter pointer for lookup

  if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0)
    return false;

  // Add to our list
  adapters_.push_back(AdapterEntry{adapter, callback, fd});
  return true;
}

bool CanEventLoop::remove(CANBusAdapter* adapter) {
  if (epfd_ < 0 || adapter == nullptr)
    return false;

  auto it = findAdapter(adapter);
  if (it == adapters_.end())
    return false;

  // Remove from epoll
  ::epoll_ctl(epfd_, EPOLL_CTL_DEL, it->fd, nullptr);

  // Remove from our list
  adapters_.erase(it);
  return true;
}

int CanEventLoop::poll(int timeoutMs) {
  if (epfd_ < 0)
    return 0;

  epoll_event events[MAX_EVENTS];
  int nfds = ::epoll_wait(epfd_, events, MAX_EVENTS, timeoutMs);

  if (nfds <= 0)
    return 0;

  int dispatched = 0;
  for (int i = 0; i < nfds; ++i) {
    auto* adapter = static_cast<CANBusAdapter*>(events[i].data.ptr);

    // Find the callback for this adapter
    auto it = findAdapter(adapter);
    if (it != adapters_.end() && it->callback) {
      it->callback(adapter, events[i].events);
      ++dispatched;
    }
  }

  return dispatched;
}

auto CanEventLoop::findAdapter(CANBusAdapter* adapter) -> decltype(adapters_.begin()) {
  return std::find_if(adapters_.begin(), adapters_.end(),
                      [adapter](const AdapterEntry& e) { return e.adapter == adapter; });
}

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex
