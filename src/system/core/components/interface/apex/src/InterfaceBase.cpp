/**
 * @file InterfaceBase.cpp
 * @brief Implementation of InterfaceBase socket I/O and pipe management.
 */

#include "src/system/core/components/interface/apex/inc/InterfaceBase.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

#include <array>
#include <string>
#include <utility>

namespace system_core {
namespace interface {

/* ----------------------------- InterfaceBase Methods ----------------------------- */

InterfaceBase::InterfaceBase() noexcept {
  status_ = Status::SUCCESS;
  isInitialized_ = false;
  numServers_ = 0U;
  maxBufferSize_ = GLOBAL_BUFFER_MAX;
  ioBuffer_.fill(static_cast<std::uint8_t>(0));
}

InterfaceBase::~InterfaceBase() = default;

/* ----------------------------- Logging ----------------------------- */

void InterfaceBase::initInterfaceLog(const std::filesystem::path& logDir) noexcept {
  componentLogPath_ = logDir / INTERFACE_LOG_FN;
  auto log = std::make_shared<logs::SystemLog>(componentLogPath_.string(),
                                               logs::SystemLog::Mode::ASYNC, 4096);
  log->setLevel(logs::SystemLog::Level::DEBUG);
  setComponentLog(std::move(log));
}

/* ----------------------------- Callbacks ----------------------------- */

void InterfaceBase::tcpClientReadableCallback(void* ctx, int clientfd) noexcept {
  auto* cbCtx = static_cast<TcpCallbackCtx*>(ctx);
  InterfaceBase* iface = cbCtx->iface;
  apex::protocols::tcp::TcpSocketServer* srv = cbCtx->srv;
  const std::uint8_t SID = cbCtx->serverId;

  // RX: drain all available data (edge-triggered epoll requires full drain)
  std::array<std::uint8_t, GLOBAL_BUFFER_MAX> rbuf{};
  std::string rerr;
  for (;;) {
    const ssize_t NREAD = srv->read(clientfd, rbuf, /*timeout ignored*/ 0, rerr);
    if (NREAD > 0) {
      apex::compat::rospan<std::uint8_t> in{rbuf.data(), static_cast<std::size_t>(NREAD)};
      iface->processBytesRx(SID, in);
    } else {
      // NREAD <= 0: EAGAIN (would block), peer closed, or error - stop draining
      break;
    }
  }

  // TX: burst a few messages per readable event
  std::array<std::uint8_t, GLOBAL_BUFFER_MAX> tbuf{};
  const std::size_t OUT_CAP = std::min(iface->maxBufferSize_, tbuf.size());
  for (std::size_t iter = 0; iter < TX_BURST_MAX; ++iter) {
    const std::size_t PRODUCED =
        iface->processBytesTx(SID, apex::compat::mutable_bytes_span{tbuf.data(), OUT_CAP});
    if (PRODUCED == 0U) {
      break;
    }
    std::string werr;
    (void)srv->write(clientfd, apex::compat::bytes_span{tbuf.data(), PRODUCED}, werr);
  }
}

/* ----------------------------- Lifecycle ----------------------------- */

Status InterfaceBase::configureSockets(const SocketConfiguration& cfg) noexcept {
  if (COMPAT_UNLIKELY(isInitialized_)) {
    status_ = Status::ERROR_ALREADY_INITIALIZED;
    return status_;
  }

  // Validate bundle.
  const std::size_t EP_COUNT = cfg.endpoints.size();
  if (COMPAT_UNLIKELY(EP_COUNT > MAX_SERVERS)) {
    status_ = Status::ERROR_CONFIG;
    return status_;
  }
  if (COMPAT_UNLIKELY(cfg.maxIoBufferBytes == 0U || cfg.maxIoBufferBytes > GLOBAL_BUFFER_MAX)) {
    status_ = Status::ERROR_CONFIG;
    return status_;
  }
  for (std::size_t i = 0; i < EP_COUNT; ++i) {
    if (COMPAT_UNLIKELY(cfg.endpoints[i].port == 0U)) {
      status_ = Status::ERROR_CONFIG;
      return status_;
    }
  }

  // Stage config and reset server contexts.
  ioCfg_ = cfg.ioConfig;
  maxBufferSize_ = cfg.maxIoBufferBytes;

  numServers_ = EP_COUNT;
  for (std::size_t i = 0; i < numServers_; ++i) {
    endpoints_[i] = cfg.endpoints[i];

    auto& s = servers_[i];
    s.serverId = static_cast<std::uint8_t>(i);
    s.transport = endpoints_[i].transport;
    s.rxErrorCount = 0U;
    s.txErrorCount = 0U;
    s.handle = ServerHandle{};

    // Allocate lock-free SPSC queues (NOT RT-safe, done during config)
    const std::size_t CAP =
        ioCfg_.pipeCapacityMessages > 0 ? ioCfg_.pipeCapacityMessages : DEFAULT_PIPE_CAPACITY;
    s.rxPipe = std::make_unique<apex::concurrency::SPSCQueue<std::vector<std::uint8_t>>>(CAP);
    s.txPipe = std::make_unique<apex::concurrency::SPSCQueue<MessageBuffer*>>(CAP);
  }

  // Pre-allocate TX buffer pool (sized to pipe capacity so pool never exhausts under normal load).
  const std::size_t TX_POOL_CAP =
      ioCfg_.pipeCapacityMessages > 0 ? ioCfg_.pipeCapacityMessages : DEFAULT_PIPE_CAPACITY;
  txPool_ = std::make_unique<BufferPool>(TX_POOL_CAP, maxBufferSize_);

  // Cold path: construct/initialize servers.
  if (COMPAT_UNLIKELY(numServers_ == 0U)) {
    isInitialized_ = true;
    status_ = Status::SUCCESS;
    if (componentLog()) {
      componentLog()->info(label(), "InterfaceBase configured (no servers)");
    }
    return status_;
  }

  const std::size_t N = numServers_;
  for (std::size_t i = 0; i < N; ++i) {
    const SocketEndpoint& ep = endpoints_[i];
    const std::string PORT_STR = std::to_string(static_cast<unsigned int>(ep.port));

    if (ep.transport == SocketTransport::TCP) {
      auto srv = std::make_unique<apex::protocols::tcp::TcpSocketServer>(ep.bindAddress, PORT_STR);
      std::string err;
      const std::uint8_t CODE = srv->init(err);
      if (COMPAT_UNLIKELY(CODE != apex::protocols::tcp::TCP_SERVER_SUCCESS)) {
        status_ = Status::ERROR_CREATE_SERVER;
        if (componentLog()) {
          componentLog()->error(label(), static_cast<std::uint8_t>(status_),
                                "TCP server init failed: " + err);
        }
        return status_;
      }

      servers_[i].handle = std::move(srv);
      auto* srvPtr =
          std::get<std::unique_ptr<apex::protocols::tcp::TcpSocketServer>>(servers_[i].handle)
              .get();

      servers_[i].tcpCbCtx.iface = this;
      servers_[i].tcpCbCtx.srv = srvPtr;
      servers_[i].tcpCbCtx.serverId = static_cast<std::uint8_t>(i);

      srvPtr->setOnClientReadable(
          apex::concurrency::Delegate<void, int>{tcpClientReadableCallback, &servers_[i].tcpCbCtx});
      if (componentLog()) {
        componentLog()->info(label(), "TCP server up on " + ep.bindAddress + ":" + PORT_STR);
      }
    } else {
      auto srv = std::make_unique<apex::protocols::udp::UdpSocketServer>(ep.bindAddress, PORT_STR);
      std::string err;
      const std::uint8_t CODE = srv->init(err);
      if (COMPAT_UNLIKELY(CODE != apex::protocols::udp::UDP_SERVER_SUCCESS)) {
        status_ = Status::ERROR_CREATE_SERVER;
        if (componentLog()) {
          componentLog()->error(label(), static_cast<std::uint8_t>(status_),
                                "UDP server init failed: " + err);
        }
        return status_;
      }
      servers_[i].handle = std::move(srv);
      if (componentLog()) {
        componentLog()->info(label(), "UDP server up on " + ep.bindAddress + ":" + PORT_STR);
      }
    }
  }

  isInitialized_ = true;
  status_ = Status::SUCCESS;
  if (componentLog()) {
    componentLog()->info(label(), "InterfaceBase configured");
  }
  return status_;
}

Status InterfaceBase::shutdown() noexcept {
  if (COMPAT_UNLIKELY(!isInitialized_)) {
    status_ = Status::SUCCESS;
    return status_;
  }

  const std::size_t N = numServers_;
  for (std::size_t i = 0; i < N; ++i) {
    auto& s = servers_[i];

    if (s.transport == SocketTransport::TCP) {
      auto* p = std::get_if<std::unique_ptr<apex::protocols::tcp::TcpSocketServer>>(&s.handle);
      if (p != nullptr && p->get() != nullptr) {
        (*p)->stop();
      }
    } else {
      auto* p = std::get_if<std::unique_ptr<apex::protocols::udp::UdpSocketServer>>(&s.handle);
      if (p != nullptr && p->get() != nullptr) {
        (*p)->stop();
      }
    }

    s.handle = ServerHandle{};
    // Drain TX pipe and release buffers back to pool before destroying.
    if (s.txPipe && txPool_) {
      MessageBuffer* buf = nullptr;
      while (s.txPipe->tryPop(buf)) {
        if (buf != nullptr) {
          txPool_->release(buf);
        }
      }
    }
    // Release SPSC queues (NOT RT-safe, done during shutdown)
    s.rxPipe.reset();
    s.txPipe.reset();
    s.rxErrorCount = 0U;
    s.txErrorCount = 0U;
  }

  // Release TX buffer pool.
  txPool_.reset();

  isInitialized_ = false;
  status_ = Status::SUCCESS;
  if (componentLog()) {
    componentLog()->info(label(), "InterfaceBase shutdown complete");
  }
  return status_;
}

/* ----------------------------- Flushing ----------------------------- */

void InterfaceBase::flushTxToClients(std::uint8_t serverId,
                                     apex::protocols::tcp::TcpSocketServer* srv) noexcept {
  if (COMPAT_UNLIKELY(srv == nullptr || serverId >= numServers_)) {
    return;
  }
  auto& s = servers_[serverId];
  if (COMPAT_UNLIKELY(!s.txPipe)) {
    return;
  }

  // Process pending TX data and encode to buffer
  std::array<std::uint8_t, GLOBAL_BUFFER_MAX> tbuf{};
  const std::size_t OUT_CAP = std::min(maxBufferSize_, tbuf.size());

  // Burst multiple messages per flush
  for (std::size_t iter = 0; iter < TX_BURST_MAX * 2; ++iter) {
    const std::size_t PRODUCED =
        processBytesTx(serverId, apex::compat::mutable_bytes_span{tbuf.data(), OUT_CAP});
    if (PRODUCED == 0U) {
      break;
    }
    // Write to ALL connected clients (broadcast pattern for now)
    srv->writeAll(apex::compat::bytes_span{tbuf.data(), PRODUCED});
  }
}

/* ----------------------------- Polling ----------------------------- */

COMPAT_HOT
void InterfaceBase::pollSockets(int timeoutMs) noexcept {
  if (COMPAT_UNLIKELY(!isInitialized_)) {
    return;
  }
  const std::size_t N = numServers_;
  for (std::size_t i = 0; i < N; ++i) {
    auto& s = servers_[i];
    if (s.transport == SocketTransport::TCP) {
      auto* p = std::get_if<std::unique_ptr<apex::protocols::tcp::TcpSocketServer>>(&s.handle);
      if (p != nullptr && p->get() != nullptr) {
        (*p)->processEvents(timeoutMs);
        // Flush pending TX to all connected clients after processing events.
        // This ensures responses are sent even without new RX data.
        flushTxToClients(static_cast<std::uint8_t>(i), p->get());
      }
    } else {
      auto* p = std::get_if<std::unique_ptr<apex::protocols::udp::UdpSocketServer>>(&s.handle);
      if (p != nullptr && p->get() != nullptr) {
        (*p)->processEvents(timeoutMs);
      }
    }
  }
}

COMPAT_HOT
void InterfaceBase::pollSocket(std::uint8_t serverId, int timeoutMs) noexcept {
  if (COMPAT_UNLIKELY(!isInitialized_ || serverId >= numServers_)) {
    return;
  }
  auto& s = servers_[serverId];
  if (s.transport == SocketTransport::TCP) {
    auto* p = std::get_if<std::unique_ptr<apex::protocols::tcp::TcpSocketServer>>(&s.handle);
    if (p != nullptr && p->get() != nullptr) {
      (*p)->processEvents(timeoutMs);
    }
  } else {
    auto* p = std::get_if<std::unique_ptr<apex::protocols::udp::UdpSocketServer>>(&s.handle);
    if (p != nullptr && p->get() != nullptr) {
      (*p)->processEvents(timeoutMs);
    }
  }
}

/* ----------------------------- Pipe Operations ----------------------------- */

COMPAT_HOT
bool InterfaceBase::popRxMessage(std::uint8_t serverId,
                                 std::vector<std::uint8_t>& outMsg) noexcept {
  if (COMPAT_UNLIKELY(serverId >= numServers_)) {
    return false;
  }
  auto& s = servers_[serverId];
  if (COMPAT_UNLIKELY(!s.rxPipe)) {
    return false;
  }
  // Lock-free: single consumer (task execution thread)
  return s.rxPipe->tryPop(outMsg);
}

COMPAT_HOT
void InterfaceBase::enqueueTxMessage(std::uint8_t serverId,
                                     apex::compat::rospan<std::uint8_t> data) noexcept {
  if (COMPAT_UNLIKELY(serverId >= numServers_ || data.empty() || !txPool_)) {
    return;
  }
  auto& s = servers_[serverId];
  if (COMPAT_UNLIKELY(!s.txPipe)) {
    return;
  }
  // Acquire pre-allocated buffer from pool (RT-safe, lock-free).
  MessageBuffer* buf = txPool_->acquire(data.size());
  if (COMPAT_UNLIKELY(buf == nullptr)) {
    return; // Pool exhausted (all buffers in flight).
  }
  // Copy only the actual message bytes (not 4KB).
  std::memcpy(buf->data, data.data(), data.size());
  buf->length = data.size();
  // Push pointer to pipe (8 bytes, trivial move).
  if (COMPAT_UNLIKELY(!s.txPipe->tryPush(buf))) {
    txPool_->release(buf); // Pipe full - return buffer to pool.
  }
}

COMPAT_HOT
void InterfaceBase::pushRxMessage(std::uint8_t serverId, std::vector<std::uint8_t>&& msg) noexcept {
  if (COMPAT_UNLIKELY(serverId >= numServers_)) {
    return;
  }
  auto& s = servers_[serverId];
  if (COMPAT_UNLIKELY(!s.rxPipe)) {
    return;
  }
  // Lock-free: single producer (externalIO thread via processBytesRx callback)
  // If queue is full, drop the message (bounded queue behavior)
  (void)s.rxPipe->tryPush(std::move(msg));
}

COMPAT_HOT
bool InterfaceBase::tryDequeueTxMessage(std::uint8_t serverId, MessageBuffer*& outBuf) noexcept {
  if (COMPAT_UNLIKELY(serverId >= numServers_)) {
    return false;
  }
  auto& s = servers_[serverId];
  if (COMPAT_UNLIKELY(!s.txPipe)) {
    return false;
  }
  // Lock-free: single consumer (externalIO thread via tcpClientReadableCallback)
  return s.txPipe->tryPop(outBuf);
}

COMPAT_HOT
void InterfaceBase::releaseTxBuffer(MessageBuffer* buf) noexcept {
  if (COMPAT_LIKELY(txPool_ && buf != nullptr)) {
    txPool_->release(buf);
  }
}

} // namespace interface
} // namespace system_core
