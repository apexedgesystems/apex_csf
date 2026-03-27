#ifndef APEX_PROTOCOLS_CCSDS_EPP_PROCESSOR_HPP
#define APEX_PROTOCOLS_CCSDS_EPP_PROCESSOR_HPP
/**
 * @file CcsdsEppProcessor.hpp
 * @brief Incremental extractor for CCSDS EPP packets from a byte stream.
 *
 * RT-Safety:
 *  - No heap allocation after construction (fixed-size buffer via template parameter).
 *  - No std::function (uses apex::concurrency::Delegate for callbacks).
 *  - No exceptions in hot paths.
 *  - Compact status codes with ERROR_/WARNING_ prefixes (cold-path toString()).
 *  - Optional zero-copy delivery via callback; otherwise packets are parsed and discarded.
 *  - Sliding-window buffer with compaction (no O(n) front erases).
 *  - C++17-compatible via apex::compat::bytes_span.
 *
 * Packet length rules per CCSDS 133.1-B-3 Section 4.1.2.4:
 *  - LoL=00 (1-octet header): Total length is 1 (idle packet)
 *  - LoL=01 (2-octet header): Packet Length field is 1 octet
 *  - LoL=10 (4-octet header): Packet Length field is 2 octets
 *  - LoL=11 (8-octet header): Packet Length field is 4 octets
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppViewer.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace protocols {
namespace ccsds {
namespace epp {

/* ----------------------------- Status & Config ---------------------------- */

/**
 * @enum Status
 * @brief Processor return/status codes.
 */
enum class Status : std::uint8_t {
  OK = 0,                 ///< Normal completion; 0+ packets may have been extracted.
  NEED_MORE,              ///< Not enough bytes to complete the next packet.
  WARNING_DESYNC_DROPPED, ///< Dropped 1+ bytes to resync (invalid version/header).
  ERROR_LENGTH_OVER_MAX,  ///< Header advertises a packet longer than configured max.
  ERROR_BUFFER_FULL       ///< Input dropped because fixed buffer is full.
};

/**
 * @brief Cold-path helper to stringify Status.
 * @param s Status code.
 * @return const char* static string.
 * @note RT-safe: Returns static string literal.
 */
const char* toString(Status s) noexcept;

/* ----------------------------- Callback Types ----------------------------- */

/**
 * @brief RT-safe packet callback delegate.
 *
 * Signature: void(void* ctx, apex::compat::bytes_span packet) noexcept
 *
 * @note RT-safe: No heap allocation, no type erasure.
 */
using PacketDelegate = apex::concurrency::Delegate<void, apex::compat::bytes_span>;

/**
 * @brief RT-safe high-watermark callback delegate.
 *
 * Signature: void(void* ctx, std::size_t bufferedBytes) noexcept
 *
 * @note RT-safe: No heap allocation, no type erasure.
 */
using HighWatermarkDelegate = apex::concurrency::Delegate<void, std::size_t>;

/* ----------------------------- ProcessorConfig ---------------------------- */

/**
 * @struct ProcessorConfig
 * @brief Behavior/configuration knobs for the stream processor.
 *
 * @note RT-safe: POD struct, no heap allocation.
 */
struct ProcessorConfig {
  /// Upper bound guard for a single packet.
  std::size_t maxPacketLength = EPP_DEFAULT_MAX_PACKET_LENGTH;

  /// On invalid version or oversize, drop bytes until a plausible header.
  bool dropUntilValidHeader = true;

  /// Sliding-window compaction policy (to avoid unbounded head growth).
  /// Compaction occurs when (head_ >= compactThreshold) AND (head_ >= bufferLen_/2).
  std::size_t compactThreshold = 1024;

  /// Optional soft high-watermark for buffered bytes; if >0 and crossed after append(),
  /// the processor invokes highWatermarkCallback.
  std::size_t highWatermarkBytes = 0; ///< 0 disables watermark checks.

  /// RT-safe high-watermark callback.
  HighWatermarkDelegate highWatermarkCallback{};
};

/* ----------------------------- ProcessResult ------------------------------ */

/**
 * @struct ProcessResult
 * @brief Per-call outcome summary.
 *
 * @note RT-safe: POD struct.
 */
struct ProcessResult {
  Status status = Status::OK;       ///< Final status for this call.
  std::size_t bytesConsumed = 0;    ///< Number of input bytes appended.
  std::size_t packetsExtracted = 0; ///< Packets delivered during this call.
  std::size_t resyncDrops = 0;      ///< Bytes dropped for resync in this call.
};

/* -------------------------------- Telemetry ------------------------------- */

/**
 * @struct Counters
 * @brief Lightweight running totals (resettable).
 *
 * @note RT-safe: POD struct.
 */
struct Counters {
  std::size_t totalBytesIn = 0;          ///< Cumulative bytes appended via process().
  std::size_t totalPacketsExtracted = 0; ///< Cumulative packets extracted (all calls).
  std::size_t totalResyncDrops = 0;      ///< Cumulative resync drop bytes.
  std::size_t totalCalls = 0;            ///< Number of process() calls.
};

/* -------------------------------- Processor ------------------------------- */

/**
 * @class Processor
 * @brief RT-safe streaming extractor for CCSDS EPP packets.
 *
 * Input is appended to an internal fixed-size sliding window. Complete packets are
 * identified using LoL-based header length and Packet Length field, then delivered
 * via an optional zero-copy callback.
 *
 * @tparam BufferSize Fixed internal buffer capacity in bytes.
 *
 * @note RT-safe: No heap allocation after construction. All operations are O(n)
 *       where n is the input size or buffer size.
 */
template <std::size_t BufferSize = 8192> class Processor {
  static_assert(BufferSize >= EPP_HEADER_8_OCTET + 1,
                "BufferSize must hold at least minimum packet");

public:
  /**
   * @brief Default constructor.
   * @note RT-safe: No allocation.
   */
  Processor() noexcept = default;

  /**
   * @brief Configure behavior (copy-by-value; safe to call between process() calls).
   * @param cfg Configuration to apply.
   * @note RT-safe: Simple copy.
   */
  void setConfig(const ProcessorConfig& cfg) noexcept { cfg_ = cfg; }

  /**
   * @brief Get current configuration snapshot.
   * @return Copy of current configuration.
   * @note RT-safe: Simple copy.
   */
  [[nodiscard]] ProcessorConfig config() const noexcept { return cfg_; }

  /**
   * @brief Get buffer capacity.
   * @return Fixed buffer size in bytes.
   * @note RT-safe: Compile-time constant.
   */
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return BufferSize; }

  /**
   * @brief Append incoming bytes and extract as many complete packets as available.
   *
   * If a packet callback is installed, each packet is delivered zero-copy.
   * Otherwise packets are parsed and discarded (counted in the result).
   *
   * @param bytes Incoming chunk (read-only view).
   * @return Per-call ProcessResult with counts and status.
   * @note RT-safe: No allocation, bounded execution.
   */
  [[nodiscard]] ProcessResult process(apex::compat::bytes_span bytes) noexcept COMPAT_HOT {
    ProcessResult r{};

    // Append new bytes to the sliding window (up to capacity).
    if (!bytes.empty()) {
      const std::size_t live = bufferLen_ - head_;
      const std::size_t available = BufferSize - live;

      // Compact if needed to make room.
      if (bytes.size() > available && head_ > 0) {
        compact();
      }

      const std::size_t liveAfterCompact = bufferLen_ - head_;
      const std::size_t availableAfterCompact = BufferSize - liveAfterCompact;
      const std::size_t toCopy =
          (bytes.size() < availableAfterCompact) ? bytes.size() : availableAfterCompact;

      if (toCopy > 0) {
        std::memcpy(buffer_.data() + bufferLen_, bytes.data(), toCopy);
        bufferLen_ += toCopy;
        r.bytesConsumed = toCopy;
      }

      // If we couldn't fit all input, signal buffer full.
      if (toCopy < bytes.size()) {
        r.status = Status::ERROR_BUFFER_FULL;
      }

      // High-watermark check.
      const std::size_t buffered = bufferLen_ - head_;
      if (cfg_.highWatermarkBytes > 0 && buffered >= cfg_.highWatermarkBytes &&
          cfg_.highWatermarkCallback) {
        cfg_.highWatermarkCallback(buffered);
      }
    }

    // Update counters.
    counters_.totalBytesIn += r.bytesConsumed;
    ++counters_.totalCalls;

    // Extract as many complete packets as available.
    for (;;) {
      // If we don't even have 1 byte, stop early.
      if ((bufferLen_ - head_) < MIN_EPP_PACKET_LENGTH) {
        if (r.packetsExtracted == 0 && (bufferLen_ - head_) > 0 && r.status == Status::OK) {
          r.status = Status::NEED_MORE;
        }
        break;
      }

      Step s = extractOnce();

      if (s.extracted) {
        ++r.packetsExtracted;
        ++counters_.totalPacketsExtracted;
        continue;
      }

      // Accumulate desync drops and propagate status.
      r.resyncDrops += s.drops;
      counters_.totalResyncDrops += s.drops;
      if (r.status == Status::OK) {
        r.status = s.status;
      }

      // If we dropped bytes to resync, immediately try the next candidate.
      if (s.status == Status::WARNING_DESYNC_DROPPED) {
        continue;
      }

      // NEED_MORE or a hard error without drop policy -> stop.
      break;
    }

    // Prefer OK if we successfully extracted at least one packet in this call.
    if (r.packetsExtracted > 0 && r.status != Status::ERROR_BUFFER_FULL) {
      r.status = Status::OK;
    }

    // Periodic compaction to bound memmove cost over long streams.
    maybeCompact();

    return r;
  }

  /**
   * @brief Reset internal buffer (does not change config or callback).
   * @note RT-safe: Simple reset.
   */
  void reset() noexcept {
    bufferLen_ = 0;
    head_ = 0;
  }

  /**
   * @brief Get number of buffered (yet-to-be-parsed) bytes.
   * @return Live bytes in buffer.
   * @note RT-safe: Simple computation.
   */
  [[nodiscard]] std::size_t bufferedSize() const noexcept { return (bufferLen_ - head_); }

  /**
   * @brief Set an optional per-packet callback (zero-copy delivery).
   *
   * Each extracted packet is delivered as apex::compat::bytes_span over the
   * internal buffer, valid only until process() returns.
   * Copy inside your callback if you need ownership.
   *
   * @param cb Delegate with function pointer and context.
   * @note RT-safe: No allocation.
   */
  void setPacketCallback(PacketDelegate cb) noexcept { onPacket_ = cb; }

  /**
   * @brief Set packet callback using raw function pointer and context.
   * @param fn Function pointer: void(void*, apex::compat::bytes_span) noexcept.
   * @param ctx Context pointer passed to fn.
   * @note RT-safe: No allocation.
   */
  void setPacketCallback(PacketDelegate::Fn fn, void* ctx) noexcept {
    onPacket_ = PacketDelegate{fn, ctx};
  }

  /**
   * @brief Get running totals.
   * @return Copy of counters.
   * @note RT-safe: Simple copy.
   */
  [[nodiscard]] Counters counters() const noexcept { return counters_; }

  /**
   * @brief Reset running totals to zero.
   * @note RT-safe: Simple reset.
   */
  void resetCounters() noexcept { counters_ = Counters{}; }

private:
  /// Attempt to extract exactly one packet from the sliding window.
  struct Step {
    bool extracted = false;
    std::size_t drops = 0;
    Status status = Status::OK;
  };

  Step extractOnce() noexcept COMPAT_HOT {
    Step step{};

    const std::size_t live = bufferLen_ - head_;
    if (live < MIN_EPP_PACKET_LENGTH) {
      step.status = Status::NEED_MORE;
      return step;
    }

    const std::uint8_t* base = buffer_.data() + head_;

    // Check version first (must be 7 for EPP).
    const std::uint8_t version = (base[0] >> EPP_VERSION_SHIFT) & EPP_VERSION_MASK_3BIT;
    if (version != EPP_VALID_VERSION) {
      if (cfg_.dropUntilValidHeader) {
        ++head_;
        step.drops = 1;
        step.status = Status::WARNING_DESYNC_DROPPED;
      } else {
        step.status = Status::NEED_MORE; // Stall on invalid
      }
      return step;
    }

    // Determine header length from LoL field.
    const std::uint8_t lol = base[0] & EPP_LOL_MASK;
    const std::size_t hdrLen = PacketViewer::headerLengthFromLoL(lol);

    // Wait for full header.
    if (live < hdrLen) {
      step.status = Status::NEED_MORE;
      return step;
    }

    // Compute total packet length based on LoL.
    std::size_t totalLength = 0;
    if (lol == EPP_LOL_IDLE) {
      // Idle packet: total length is 1 (header only, no payload).
      totalLength = 1;
    } else {
      // Read Packet Length field.
      totalLength = readPacketLength(base, lol);
    }

    // Guard: cap maximum advertised packet length.
    if (totalLength > cfg_.maxPacketLength) {
      step.status = Status::ERROR_LENGTH_OVER_MAX;

      if (cfg_.dropUntilValidHeader) {
        // Drop header to jump to next plausible candidate.
        const std::size_t drop = (live >= hdrLen) ? hdrLen : live;
        head_ += drop;
        step.drops = drop;
        step.status = Status::WARNING_DESYNC_DROPPED;
      }
      return step;
    }

    // Sanity: packet length must be at least header length.
    if (totalLength < hdrLen) {
      if (cfg_.dropUntilValidHeader) {
        ++head_;
        step.drops = 1;
        step.status = Status::WARNING_DESYNC_DROPPED;
      }
      return step;
    }

    // Not enough bytes yet for the full packet.
    if (live < totalLength) {
      step.status = Status::NEED_MORE;
      return step;
    }

    // We have a complete packet [head_, head_ + totalLength).
    if (onPacket_) {
      onPacket_(apex::compat::bytes_span{base, totalLength});
    }

    // Consume the extracted packet from the front of the window.
    head_ += totalLength;

    step.extracted = true;
    step.status = Status::OK;
    return step;
  }

  static std::size_t readPacketLength(const std::uint8_t* base, std::uint8_t lol) noexcept {
    switch (lol) {
    case EPP_LOL_1_OCTET:
      return base[1];
    case EPP_LOL_2_OCTETS:
      return static_cast<std::size_t>((static_cast<std::uint16_t>(base[2]) << 8) | base[3]);
    case EPP_LOL_4_OCTETS:
      return static_cast<std::size_t>((static_cast<std::uint32_t>(base[4]) << 24) |
                                      (static_cast<std::uint32_t>(base[5]) << 16) |
                                      (static_cast<std::uint32_t>(base[6]) << 8) | base[7]);
    default:
      return 0;
    }
  }

  void maybeCompact() noexcept {
    if (head_ >= cfg_.compactThreshold && head_ >= (bufferLen_ / 2)) {
      compact();
    }
  }

  void compact() noexcept {
    const std::size_t live = bufferLen_ - head_;
    if (live > 0 && head_ > 0) {
      std::memmove(buffer_.data(), buffer_.data() + head_, live);
    }
    bufferLen_ = live;
    head_ = 0;
  }

  ProcessorConfig cfg_{};
  std::array<std::uint8_t, BufferSize> buffer_{}; ///< Fixed-size sliding-window storage.
  std::size_t bufferLen_ = 0;                     ///< Valid bytes in buffer.
  std::size_t head_ = 0;                          ///< Read index into buffer_.
  PacketDelegate onPacket_{};                     ///< Optional zero-copy delivery.
  Counters counters_{};                           ///< Telemetry (cold-path).
};

/* ----------------------------- Type Aliases ------------------------------- */

/// Small processor (4KB buffer).
using ProcessorSmall = Processor<4096>;

/// Default processor (8KB buffer).
using ProcessorDefault = Processor<8192>;

/// Large processor (16KB buffer).
using ProcessorLarge = Processor<16384>;

} // namespace epp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_EPP_PROCESSOR_HPP
