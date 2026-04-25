#ifndef APEX_SYSTEM_CORE_INTERFACE_BASE_HPP
#define APEX_SYSTEM_CORE_INTERFACE_BASE_HPP
/**
 * @file InterfaceBase.hpp
 * @brief Base interface for socket I/O (TCP/UDP), per-server RX/TX pipes, and a shared I/O buffer
 * (C++17).
 *
 * Notes:
 * - Inherits from SystemComponentBase for component identity and logging.
 * - Returns typed Status; internal ops use error-code paths.
 * - configureSockets() performs network setup; not real-time safe. Reconfigure requires shutdown().
 * - Accepting new TCP connections is handled inside each server's event loop (processEvents()).
 */

#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceSocketConfiguration.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceStatus.hpp"
#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"

#include <cstddef> // std::size_t
#include <cstdint> // std::uint8_t

#include <array>
#include <filesystem>
#include <memory>  // std::shared_ptr, std::unique_ptr
#include <string>  // std::string
#include <utility> // std::move
#include <variant> // std::variant
#include <vector>  // std::vector

namespace system_core {
namespace interface {

/* ----------------------------- InterfaceBase ----------------------------- */

/**
 * @class InterfaceBase
 * @brief Owns up to MAX_SERVERS TCP/UDP servers, per-server RX/TX pipes, and a shared I/O buffer.
 *        Derived classes implement processing via processBytesRx/Tx(); base exposes poll ticks.
 *        Inherits from SystemComponentBase for component identity, logging, and command handling.
 */
class InterfaceBase : public system_component::CoreComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (4 = Interface in system component range).
  static constexpr std::uint16_t COMPONENT_ID = 4;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Interface";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Constants ----------------------------- */

  /// Maximum number of servers supported by this interface.
  static constexpr std::size_t MAX_SERVERS = 10;

  /// Maximum size of the shared I/O buffer (bytes). Configurable via tunables.
  static constexpr std::size_t GLOBAL_BUFFER_MAX = 4096;

  /// Per-event TX burst cap: how many messages we attempt to serialize/write in one readable
  /// callback. Keeps latency low without spinning; tweakable for workloads.
  static constexpr std::size_t TX_BURST_MAX = 4;

  /// Interface log filename.
  static constexpr const char* INTERFACE_LOG_FN = "interface.log";

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  InterfaceBase() noexcept;

  /// Out-of-line destructor (defined in .cpp where server types are complete).
  ~InterfaceBase() override;

  /// Module label (string literal).
  [[nodiscard]] const char* label() const noexcept override { return "INTERFACE"; }

  /// Last interface operation status (typed).
  [[nodiscard]] Status interfaceStatus() const noexcept { return status_; }

  /// True after successful configureSockets(); false after shutdown() or before configuration.
  [[nodiscard]] bool isSocketsConfigured() const noexcept { return isInitialized_; }

  /// Maximum configured I/O buffer size (bytes).
  std::size_t maxBufferSize() const noexcept { return maxBufferSize_; }

  /// Current I/O config (copy).
  SocketIoConfig ioConfig() const noexcept { return ioCfg_; }

  /// Number of active servers.
  std::size_t numServers() const noexcept { return numServers_; }

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Configure and bring up servers/buffers.
   *        If already initialized, returns ERROR_ALREADY_INITIALIZED. Call shutdown() first to
   * reconfigure.
   *
   * Preconditions:
   * - cfg.endpoints.size() <= MAX_SERVERS
   * - cfg.maxIoBufferBytes in (0, GLOBAL_BUFFER_MAX]
   *
   * @note NOT RT-safe: Allocates memory, performs network bind/listen.
   */
  [[nodiscard]] virtual Status configureSockets(const SocketConfiguration& cfg) noexcept;

  /**
   * @brief Tear down servers and clear pipes/buffers. Idempotent.
   * @return Status result.
   * @note NOT RT-safe: Closes sockets, deallocates pipes.
   */
  [[nodiscard]] virtual Status shutdown() noexcept;

  /* ----------------------------- Scheduler Entry Points ----------------------------- */

  /**
   * @brief Advance all servers' event loops once.
   *        This handles accepting new TCP connections, readable/writable I/O, and will invoke the
   *        derived hooks (processBytesRx/Tx) via registered callbacks.
   * @param timeoutMs Poll timeout per server tick (ms).
   * @note RT-safe: No allocation; bounded poll + callback dispatch.
   */
  void pollSockets(int timeoutMs) noexcept;

  /**
   * @brief Advance one server's event loop once (0 <= serverId < numServers()).
   *        Same semantics as pollSockets() but scoped to a single server.
   * @param serverId Server index.
   * @param timeoutMs Poll timeout (ms).
   * @note RT-safe: No allocation; bounded poll + callback dispatch.
   */
  void pollSocket(std::uint8_t serverId, int timeoutMs) noexcept;

  /* ----------------------------- Pipe Helpers ----------------------------- */

  /**
   * @brief Pop one decoded RX message from a server pipe.
   * @note RT-safe: Lock-free SPSC pop.
   */
  [[nodiscard]] bool popRxMessage(std::uint8_t serverId,
                                  std::vector<std::uint8_t>& outMsg) noexcept;

  /**
   * @brief Enqueue one TX message into a server pipe (bounded; drops on overflow).
   * @param serverId Server index.
   * @param data     Encoded message bytes to send.
   * @note RT-safe: Lock-free acquire from txPool + pointer push (no heap allocation).
   */
  void enqueueTxMessage(std::uint8_t serverId, apex::compat::rospan<std::uint8_t> data) noexcept;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Initialize interface log. Call before configure().
   * @param logDir Directory for interface log file.
   * @note NOT RT-safe: Opens log file.
   */
  void initInterfaceLog(const std::filesystem::path& logDir) noexcept;

protected:
  /* ----------------------------- Hooks ----------------------------- */

  /**
   * @brief RX hook: process inbound raw bytes for a server (decode, frame, etc.).
   *        Implementations typically push complete application messages into the server's rxPipe.
   * @param serverId Server index.
   * @param rxData   View over received bytes.
   */
  virtual void processBytesRx(std::uint8_t serverId, apex::compat::rospan<std::uint8_t> rxData) = 0;

  /**
   * @brief TX hook: produce bytes to send for a server (encode, frame, etc.).
   *        Implementations typically pull messages from the server's txPipe and serialize into
   * outBuf.
   * @param serverId Server index.
   * @param outBuf   Writable byte span to fill.
   * @return Number of bytes produced into outBuf.
   */
  virtual std::size_t processBytesTx(std::uint8_t serverId,
                                     apex::compat::mutable_bytes_span outBuf) = 0;

  /// Pipe capacity for a server (may reflect overrides in future).
  std::size_t pipeCapacity(std::uint8_t /*serverId*/) const noexcept {
    return ioCfg_.pipeCapacityMessages;
  }

  /**
   * @brief Override doInit() from SystemComponentBase (no-op by default for interface).
   * @return 0 on success.
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }

  /// Push a decoded app message into a server's RX pipe (bounded; drops oldest on overflow).
  void pushRxMessage(std::uint8_t serverId, std::vector<std::uint8_t>&& msg) noexcept;

  /// Try to dequeue one TX buffer pointer from a server's TX pipe (returns false if empty).
  /// Caller must release the buffer back to txPool_ after use.
  [[nodiscard]] bool tryDequeueTxMessage(std::uint8_t serverId, MessageBuffer*& outBuf) noexcept;

  /// Release a TX buffer back to the TX pool after use.
  /// @note RT-safe: Lock-free push to pool free list.
  void releaseTxBuffer(MessageBuffer* buf) noexcept;

private:
  // Polymorphic handle to either TCP or UDP server.
  using ServerHandle = std::variant<std::unique_ptr<apex::protocols::tcp::TcpSocketServer>,
                                    std::unique_ptr<apex::protocols::udp::UdpSocketServer>>;

  /// Context for RT-safe TCP readable callback (used by Delegate).
  struct TcpCallbackCtx {
    InterfaceBase* iface{nullptr};
    apex::protocols::tcp::TcpSocketServer* srv{nullptr};
    std::uint8_t serverId{0};
  };

  /// Static callback for TCP client readable events (Delegate-compatible).
  static void tcpClientReadableCallback(void* ctx, int clientfd) noexcept;

  /// Flush pending TX messages to all connected TCP clients.
  void flushTxToClients(std::uint8_t serverId, apex::protocols::tcp::TcpSocketServer* srv) noexcept;

  /// Default pipe capacity for SPSC queues.
  static constexpr std::size_t DEFAULT_PIPE_CAPACITY = 256;

  struct ServerContext {
    std::uint8_t serverId{0};
    SocketTransport transport{SocketTransport::TCP};
    ServerHandle handle{};     ///< Active server.
    TcpCallbackCtx tcpCbCtx{}; ///< Context for TCP readable callback.
    std::unique_ptr<apex::concurrency::SPSCQueue<std::vector<std::uint8_t>>>
        rxPipe; ///< Lock-free RX.
    std::unique_ptr<apex::concurrency::SPSCQueue<MessageBuffer*>>
        txPipe; ///< Lock-free TX (pointer push, zero-alloc hot path).
    std::uint32_t rxErrorCount{0};
    std::uint32_t txErrorCount{0};
  };

  Status status_{Status::SUCCESS};
  bool isInitialized_{false};

  SocketIoConfig ioCfg_{};                       ///< Shared defaults for RX/TX/pipe sizes.
  std::size_t numServers_{0};                    ///< <= MAX_SERVERS
  std::size_t maxBufferSize_{GLOBAL_BUFFER_MAX}; ///< <= GLOBAL_BUFFER_MAX

  std::array<SocketEndpoint, MAX_SERVERS> endpoints_{}; ///< Staged endpoints (configureSockets()).
  std::array<ServerContext, MAX_SERVERS> servers_{};    ///< First numServers_ are valid.
  std::array<std::uint8_t, GLOBAL_BUFFER_MAX> ioBuffer_{}; ///< Use first maxBufferSize_ bytes.

  /// Pre-allocated buffer pool for TX pipe messages (zero-alloc hot path).
  /// Sized to match pipe capacity so every slot can be filled without pool exhaustion.
  std::unique_ptr<BufferPool> txPool_;

  std::filesystem::path componentLogPath_; ///< Path to interface log file.
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_BASE_HPP
