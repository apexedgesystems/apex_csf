/**
 * @file ShmRingBridge.cpp
 * @brief Implementation of the ShmRingBridge SUPPORT component.
 *
 * Implements the Side A (producer/owner) role of a self-described wire
 * format that apex owns and documents in this lib's README (section 5).
 * Independent of any consumer -- no shared code or library; consumers
 * implement the same format on their own side.
 */

#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fmt/format.h>
#include <optional>
#include <string>

namespace system_core {
namespace support {

namespace {

/* ----------------------------- File Helpers ----------------------------- */

constexpr mode_t kShmMode = 0600;

bool isAbsolutePath(const char* path, std::size_t max_len) noexcept {
  if (path == nullptr || max_len == 0u)
    return false;
  return path[0] == '/' && path[1] != '\0';
}

bool isPowerOfTwo(std::uint32_t v) noexcept { return v != 0u && (v & (v - 1u)) == 0u; }

/// Derive the wakeup path from the shm path + "_wake" if no explicit
/// value was provided (see README section 5.4).
std::string deriveWakeup(const char* shm_path, const char* explicit_wakeup) noexcept {
  if (explicit_wakeup != nullptr && explicit_wakeup[0] != '\0') {
    return std::string(explicit_wakeup);
  }
  if (shm_path == nullptr || shm_path[0] == '\0')
    return {};
  return std::string(shm_path) + "_wake";
}

/// Bytes for one ring's region: prelude (header + 2 cursors) + slots.
/// The per-region byte count (see README section 5.1).
std::size_t shmRegionBytes(std::size_t payload_size, std::uint32_t capacity) noexcept {
  return BRIDGE_RING_PRELUDE_BYTES + static_cast<std::size_t>(capacity) * payload_size;
}

/* ----------------------------- RingHeader (mirror) ----------------------------- */

/// The ring header layout. apex owns this definition (see README
/// section 5.2); any consumer must match it byte-for-byte.
struct alignas(64) MirrorRingHeader {
  std::uint32_t framework_magic;
  std::uint16_t framework_version;
  std::uint16_t reserved0;
  std::uint32_t app_magic;
  std::uint16_t app_version;
  std::uint16_t app_reserved;
  std::uint32_t payload_size;
  std::uint32_t capacity;
  std::uint8_t pad[32];
};

static_assert(sizeof(MirrorRingHeader) == 64, "the wire format requires RingHeader = 64 bytes");
static_assert(alignof(MirrorRingHeader) == 64, "the wire format requires cache-line alignment");

} // namespace

/* ----------------------------- Construction / destruction ----------------------------- */

ShmRingBridge::~ShmRingBridge() noexcept { closeChannel(); }

/* ----------------------------- Lifecycle ----------------------------- */

bool ShmRingBridge::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
  std::error_code ec;
  if (!std::filesystem::exists(PATH, ec)) {
    return true; // Tunables stay at defaults; init can still proceed.
  }
  std::string err;
  std::optional<std::reference_wrapper<std::string>> errRef{err};
  if (!apex::helpers::files::hex2cpp(PATH.string(), tunables_.get(), errRef)) {
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: hex2cpp failed for {} ({})", PATH.string(), err));
    }
    return false;
  }
  return true;
}

std::uint8_t ShmRingBridge::doInit() noexcept {
  using system_core::data::DataCategory;

  registerTask<ShmRingBridge, &ShmRingBridge::bridgeStep>(
      static_cast<std::uint8_t>(TaskUid::BRIDGE_STEP), this, "bridgeStep");
  registerTask<ShmRingBridge, &ShmRingBridge::telemetryTick>(
      static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

  registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
               sizeof(ShmRingBridgeTunables));
  registerData(DataCategory::STATE, "state", &state_.get(), sizeof(ShmRingBridgeState));
  registerData(DataCategory::OUTPUT, "telemetry", &telemetry_.get(), sizeof(ShmRingBridgeTlm));

  auto* log = componentLog();
  if (log != nullptr) {
    const auto& p = tunables_.get();
    log->info(label(), fmt::format("init: app_magic={:#x} app_ver={} cap={} payload={} "
                                   "src_uid={:#x} src_cat={} src_len={} shm={} sem={}",
                                   p.app_magic, p.app_version, p.capacity, p.payload_size,
                                   p.source_uid, p.source_category,
                                   p.source_byte_len == 0u ? p.payload_size : p.source_byte_len,
                                   p.shm_path[0] != '\0' ? p.shm_path : "(empty)",
                                   p.wakeup_path[0] != '\0' ? p.wakeup_path : "(derived)"));
  }

  return static_cast<std::uint8_t>(Status::SUCCESS);
}

void ShmRingBridge::onBusReady() noexcept {
  // Open the shm + sem now that the registry is populated and the bus
  // is wired (so the resolver delegate can find the source component).
  // Failures stay non-fatal: the executive proceeds, the sim ticks, and
  // bridgeStep silently no-ops while channel_open == 0.
  auto* log = componentLog();

  if (!openChannel()) {
    if (log != nullptr) {
      log->info(label(), "onBusReady: channel open FAILED; bridge will idle");
    }
    return;
  }
  if (!resolveSource()) {
    if (log != nullptr) {
      log->info(label(), "onBusReady: source resolve FAILED; bridge will idle");
    }
    closeChannel();
    return;
  }

  if (log != nullptr) {
    const auto& p = tunables_.get();
    log->info(label(), fmt::format("onBusReady: channel open + source resolved "
                                   "(shm={} payload_size={} cap={})",
                                   p.shm_path, p.payload_size, p.capacity));
  }
}

void ShmRingBridge::doReset() noexcept { closeChannel(); }

/* ----------------------------- Channel open / close ----------------------------- */

bool ShmRingBridge::openChannel() noexcept {
  auto& s = state_.get();
  s.channel_open = 0u;
  const auto& p = tunables_.get();
  auto* log = componentLog();

  if (!isAbsolutePath(p.shm_path, sizeof(p.shm_path))) {
    if (log != nullptr)
      log->info(label(), "openChannel: shm_path missing or not absolute");
    return false;
  }
  if (!isPowerOfTwo(p.capacity)) {
    if (log != nullptr)
      log->info(label(), fmt::format("openChannel: capacity {} not power of two", p.capacity));
    return false;
  }
  if (p.payload_size == 0u) {
    if (log != nullptr)
      log->info(label(), "openChannel: payload_size == 0");
    return false;
  }

  // The wire format: total = both rings (one per direction).
  // Asymmetric sizing: the consumer's Channel<Out, In> instantiation may
  // use a smaller "EmptyFrame" type for the unused reverse direction, in
  // which case we MUST lay out Region B at sizeof(EmptyFrame) so the
  // consumer's local TOTAL computation matches the actual shm size.
  const std::uint32_t REVERSE_SIZE =
      (p.reverse_payload_size != 0u) ? p.reverse_payload_size : p.payload_size;
  const std::size_t REGION_A_BYTES = shmRegionBytes(p.payload_size, p.capacity);
  const std::size_t REGION_B_BYTES = shmRegionBytes(REVERSE_SIZE, p.capacity);
  const std::size_t TOTAL = REGION_A_BYTES + REGION_B_BYTES;

  // Side A: unlink stale, create exclusive, ftruncate, mmap.
  shm_unlink(p.shm_path); // stale-cleanup, ignore errors
  const int FD = shm_open(p.shm_path, O_RDWR | O_CREAT | O_EXCL, kShmMode);
  if (FD < 0) {
    if (log != nullptr)
      log->info(label(),
                fmt::format("openChannel: shm_open({}) failed: {}", p.shm_path, strerror(errno)));
    return false;
  }
  if (ftruncate(FD, static_cast<off_t>(TOTAL)) != 0) {
    if (log != nullptr)
      log->info(label(), fmt::format("openChannel: ftruncate({}) failed", TOTAL));
    close(FD);
    shm_unlink(p.shm_path);
    return false;
  }
  // Record the created object's identity so an external unlink/replace of the
  // path is detectable later (checkRegionOrphaned).
  struct stat SHM_STAT{};
  if (fstat(FD, &SHM_STAT) == 0) {
    shm_ino_ = static_cast<std::uint64_t>(SHM_STAT.st_ino);
    shm_dev_ = static_cast<std::uint64_t>(SHM_STAT.st_dev);
  }

  void* mapping = mmap(nullptr, TOTAL, PROT_READ | PROT_WRITE, MAP_SHARED, FD, 0);
  close(FD);
  if (mapping == MAP_FAILED) {
    if (log != nullptr)
      log->info(label(), "openChannel: mmap failed");
    shm_unlink(p.shm_path);
    return false;
  }

  // Zero the prelude bytes and populate the headers (see the wire
  // format, README section 5.1).
  auto* base = static_cast<std::uint8_t*>(mapping);
  std::memset(base, 0, BRIDGE_RING_PRELUDE_BYTES);
  std::memset(base + REGION_A_BYTES, 0, BRIDGE_RING_PRELUDE_BYTES);

  auto write_header = [&](std::uint8_t* region, std::uint32_t pay_sz) {
    auto* h = reinterpret_cast<MirrorRingHeader*>(region);
    h->framework_magic = BRIDGE_FRAMEWORK_MAGIC;
    h->framework_version = BRIDGE_FRAMEWORK_VERSION;
    h->reserved0 = 0;
    h->app_magic = p.app_magic;
    h->app_version = p.app_version;
    h->app_reserved = 0;
    h->payload_size = pay_sz;
    h->capacity = p.capacity;
  };
  write_header(base, p.payload_size);                // Region A: forward
  write_header(base + REGION_A_BYTES, REVERSE_SIZE); // Region B: reverse

  // Bring up the wakeup semaphore (see README section 5.4).
  const std::string SEM_PATH = deriveWakeup(p.shm_path, p.wakeup_path);
  if (SEM_PATH.empty() || SEM_PATH[0] != '/') {
    if (log != nullptr)
      log->info(label(), fmt::format("openChannel: wakeup path invalid ({})", SEM_PATH));
    munmap(mapping, TOTAL);
    shm_unlink(p.shm_path);
    return false;
  }
  sem_unlink(SEM_PATH.c_str()); // stale cleanup
  sem_t* sem = sem_open(SEM_PATH.c_str(), O_CREAT | O_EXCL, kShmMode, /*value=*/0);
  if (sem == SEM_FAILED) {
    if (log != nullptr)
      log->info(label(),
                fmt::format("openChannel: sem_open({}) failed: {}", SEM_PATH, strerror(errno)));
    munmap(mapping, TOTAL);
    shm_unlink(p.shm_path);
    return false;
  }

  // Bind the writer to Region A (the apex producer's outbound direction).
  mapping_ = mapping;
  map_size_ = TOTAL;
  sem_ = sem;
  prod_cursor_ = base + BRIDGE_RING_HEADER_BYTES;
  cons_cursor_ = base + BRIDGE_RING_HEADER_BYTES + BRIDGE_CURSOR_BYTES;
  slots_ = base + BRIDGE_RING_PRELUDE_BYTES;

  // Bind Ring B (reverse direction; consumer -> apex).
  // Apex is the *consumer* of Ring B, so it reads rx_prod_cursor (updated
  // by UE5 / Side B when it pushes) and writes rx_cons_cursor (apex's
  // own read progress). The remote side's view is mirrored.
  std::uint8_t* base_b = base + REGION_A_BYTES;
  rx_prod_cursor_ = base_b + BRIDGE_RING_HEADER_BYTES;
  rx_cons_cursor_ = base_b + BRIDGE_RING_HEADER_BYTES + BRIDGE_CURSOR_BYTES;
  rx_slots_ = base_b + BRIDGE_RING_PRELUDE_BYTES;
  rx_slot_size_ = REVERSE_SIZE;

  // Cache the wakeup path so close() can unlink it (Side A is owner).
  // Done by re-deriving in close(); avoids an extra string member.

  s.channel_open = 1u;
  return true;
}

void ShmRingBridge::closeChannel() noexcept {
  auto& s = state_.get();
  s.channel_open = 0u;
  s.source_resolved = 0u;

  if (mapping_ != nullptr) {
    munmap(mapping_, map_size_);
    mapping_ = nullptr;
    map_size_ = 0;
  }
  if (sem_ != nullptr) {
    sem_close(static_cast<sem_t*>(sem_));
    sem_ = nullptr;
  }

  // Side A is the owner: unlink shm + sem so the names are reusable -- but only
  // while the path still refers to the object we created. A path re-owned by
  // another process (we were orphaned) is never removed from under it.
  const auto& p = tunables_.get();
  if (p.shm_path[0] == '/') {
    bool ours = true;
    const int PROBE = shm_open(p.shm_path, O_RDONLY, 0);
    if (PROBE >= 0) {
      struct stat SB{};
      if (fstat(PROBE, &SB) == 0 && shm_ino_ != 0) {
        ours = (static_cast<std::uint64_t>(SB.st_ino) == shm_ino_ &&
                static_cast<std::uint64_t>(SB.st_dev) == shm_dev_);
      }
      close(PROBE);
    }
    if (ours) {
      shm_unlink(p.shm_path);
    }
  }
  shm_ino_ = 0;
  shm_dev_ = 0;
  const std::string SEM_PATH = deriveWakeup(p.shm_path, p.wakeup_path);
  if (!SEM_PATH.empty() && SEM_PATH[0] == '/') {
    sem_unlink(SEM_PATH.c_str());
  }

  prod_cursor_ = nullptr;
  cons_cursor_ = nullptr;
  slots_ = nullptr;
  rx_prod_cursor_ = nullptr;
  rx_cons_cursor_ = nullptr;
  rx_slots_ = nullptr;
  rx_slot_size_ = 0;
  source_ptr_ = nullptr;
  source_len_ = 0;
}

/* ----------------------------- Source resolve ----------------------------- */

bool ShmRingBridge::resolveSource() noexcept {
  auto& s = state_.get();
  s.source_resolved = 0u;
  source_ptr_ = nullptr;
  source_len_ = 0;

  const auto& p = tunables_.get();
  auto* log = componentLog();

  if (!resolver_.fn) {
    if (log != nullptr)
      log->info(label(), "resolveSource: no resolver wired");
    return false;
  }

  const auto CAT = static_cast<data::DataCategory>(p.source_category);
  const ResolvedSource SRC = resolver_(p.source_uid, CAT);
  if (SRC.data == nullptr || SRC.size == 0u) {
    if (log != nullptr) {
      log->info(label(), fmt::format("resolveSource: target uid={:#x} cat={} not found in registry",
                                     p.source_uid, p.source_category));
    }
    return false;
  }

  // Apply byte_offset + byte_len. byte_len == 0 means "whole block"
  // (matches DataTarget::isWholeBlock semantics).
  const std::size_t OFFSET = p.source_byte_offset;
  const std::size_t LEN = (p.source_byte_len == 0u) ? SRC.size : p.source_byte_len;
  if (OFFSET + LEN > SRC.size) {
    if (log != nullptr) {
      log->info(label(), fmt::format("resolveSource: range ({}+{}) exceeds block size {}", OFFSET,
                                     LEN, SRC.size));
    }
    return false;
  }
  if (LEN != p.payload_size) {
    if (log != nullptr) {
      log->info(label(), fmt::format("resolveSource: source byte_len {} != payload_size {}", LEN,
                                     p.payload_size));
    }
    return false;
  }

  source_ptr_ = SRC.data + OFFSET;
  source_len_ = LEN;
  s.source_resolved = 1u;
  return true;
}

/* ----------------------------- Tasks ----------------------------- */

std::uint8_t ShmRingBridge::bridgeStep() noexcept {
  auto& s = state_.get();
  ++s.tick_count;

  if (s.channel_open == 0u || source_ptr_ == nullptr) {
    return 0u; // bridge is idle; sim keeps running
  }

  // The ring push sequence (see README section 5.3).
  auto* prod = static_cast<std::atomic<std::uint64_t>*>(prod_cursor_);
  auto* cons = static_cast<std::atomic<std::uint64_t>*>(cons_cursor_);
  const auto& p = tunables_.get();

  const std::uint64_t HEAD = prod->load(std::memory_order_relaxed);
  const std::uint64_t TAIL = cons->load(std::memory_order_acquire);
  if (HEAD - TAIL >= p.capacity) {
    ++s.pushes_failed_full;
    return 0u;
  }
  std::uint8_t* SLOT = slots_ + (HEAD & (p.capacity - 1u)) * source_len_;
  std::memcpy(SLOT, source_ptr_, source_len_);
  prod->store(HEAD + 1u, std::memory_order_release);

  // Signal the wakeup semaphore (see README section 5.4).
  if (sem_post(static_cast<sem_t*>(sem_)) != 0) {
    ++s.signals_failed;
  }

  ++s.frames_published;

  // Opportunistically drain one command frame from Ring B.
  // No-op when sink_enabled=0 (default) or Ring B not bound. Capped at
  // one frame per bridgeStep to keep this RT-bounded; bursts catch up
  // naturally on subsequent ticks since postInternalCommand's queue
  // absorbs the slack.
  drainCommands();

  return 0u;
}

/* ----------------------------- command sink ----------------------------- */

void ShmRingBridge::drainCommands() noexcept {
  // Fast bail-out: feature off, channel not up, or Ring B unbound.
  const auto& p = tunables_.get();
  if (p.sink_enabled == 0u)
    return;
  if (rx_prod_cursor_ == nullptr || rx_slots_ == nullptr)
    return;
  auto* bus = internalBus();
  if (bus == nullptr)
    return;

  auto& s = state_.get();

  // Pop at most one frame this tick. (Bursts catch up on subsequent ticks.)
  auto* rx_prod = static_cast<std::atomic<std::uint64_t>*>(rx_prod_cursor_);
  auto* rx_cons = static_cast<std::atomic<std::uint64_t>*>(rx_cons_cursor_);
  const std::uint64_t HEAD = rx_prod->load(std::memory_order_acquire);
  const std::uint64_t TAIL = rx_cons->load(std::memory_order_relaxed);
  if (HEAD == TAIL)
    return; // empty

  // Read the slot bytes. Frame is APROTO application-layer:
  //   bytes 0-1   magic = "AP" (0x5041 LE) -- sanity
  //   byte  2     version (must == 1)
  //   byte  3     flags (we ignore for now; CRC bit must NOT be set)
  //   bytes 4-7   fullUid (dst component for postInternalCommand)
  //   bytes 8-9   opcode
  //   bytes 10-11 sequence (echoed back; not used here)
  //   bytes 12-13 payload_length (N)
  //   bytes 14..14+N  payload
  // See the wire format (README section 5).
  const std::uint8_t* SLOT = rx_slots_ + (TAIL & (p.capacity - 1u)) * rx_slot_size_;

  constexpr std::size_t APROTO_HDR_BYTES = 14u;
  if (rx_slot_size_ < APROTO_HDR_BYTES) {
    // Slot too small to even hold a header. This is a static config error;
    // count once and advance cursor so we don't spin on it.
    ++s.cmds_decode_errors;
    rx_cons->store(TAIL + 1u, std::memory_order_release);
    return;
  }

  // Decode header (little-endian, packed; APROTO header has no padding).
  std::uint16_t magic, opcode, sequence, payload_len;
  std::uint8_t version, flags;
  std::uint32_t dst_uid;
  std::memcpy(&magic, SLOT + 0, sizeof(magic));
  std::memcpy(&version, SLOT + 2, sizeof(version));
  std::memcpy(&flags, SLOT + 3, sizeof(flags));
  std::memcpy(&dst_uid, SLOT + 4, sizeof(dst_uid));
  std::memcpy(&opcode, SLOT + 8, sizeof(opcode));
  std::memcpy(&sequence, SLOT + 10, sizeof(sequence));
  std::memcpy(&payload_len, SLOT + 12, sizeof(payload_len));
  (void)sequence; // reserved for future use

  // Validate: magic + version + payload fits in slot.
  constexpr std::uint16_t APROTO_MAGIC_LE = 0x5041u; // "AP"
  constexpr std::uint8_t APROTO_VER = 1u;
  if (magic != APROTO_MAGIC_LE || version != APROTO_VER ||
      static_cast<std::size_t>(APROTO_HDR_BYTES) + payload_len > rx_slot_size_) {
    ++s.cmds_decode_errors;
    rx_cons->store(TAIL + 1u, std::memory_order_release);
    return;
  }

  // Build payload span pointing into the slot (no copy).
  apex::compat::rospan<std::uint8_t> payload(const_cast<std::uint8_t*>(SLOT + APROTO_HDR_BYTES),
                                             payload_len);

  // Dispatch through the internal bus. The bus routes by dst fullUid to
  // the target component's cmd queue; the target's handleCommand fires
  // on the next dispatcher tick. RT-safe (lock-free push).
  const bool OK = bus->postInternalCommand(fullUid(), // src = this bridge
                                           dst_uid, opcode, payload);
  if (!OK) {
    ++s.cmds_dispatch_errors;
  } else {
    ++s.cmds_received;
  }

  // Advance consumer cursor regardless of dispatch outcome -- the frame
  // has been consumed (decoded + attempted). Skipping would deadlock on
  // a persistent dispatch failure.
  rx_cons->store(TAIL + 1u, std::memory_order_release);
}

void ShmRingBridge::checkRegionOrphaned() noexcept {
  auto& s = state_.get();
  if (s.channel_open == 0u) {
    return;
  }
  const auto& p = tunables_.get();
  auto* log = componentLog();

  // Probe the path by name and compare against the object we created. The
  // kernel objects live on; only the NAME can be taken from us.
  bool gone = false;
  bool replaced = false;
  const int FD = shm_open(p.shm_path, O_RDONLY, 0);
  if (FD < 0) {
    gone = (errno == ENOENT);
  } else {
    struct stat SB{};
    if (fstat(FD, &SB) == 0) {
      replaced = (static_cast<std::uint64_t>(SB.st_ino) != shm_ino_ ||
                  static_cast<std::uint64_t>(SB.st_dev) != shm_dev_);
    }
    close(FD);
  }

  if (!gone && !replaced) {
    return;
  }

  if (s.region_orphaned == 0u) {
    s.region_orphaned = 1u;
    if (log != nullptr) {
      log->info(label(), fmt::format("shm path {} externally: channel orphaned (writes land in "
                                     "a mapping no consumer can open)",
                                     gone ? "unlinked" : "replaced"));
    }
  }

  // Reclaim only when the name is free -- never unlink a path another process
  // now owns. closeChannel unlinks our (already-unlinked) objects harmlessly.
  if (gone && p.orphan_reclaim != 0u) {
    closeChannel();
    if (openChannel() && resolveSource()) {
      s.region_orphaned = 0u;
      if (log != nullptr) {
        log->info(label(), "orphan reclaim: channel reopened on the freed path");
      }
    } else if (log != nullptr) {
      log->info(label(), "orphan reclaim FAILED; bridge will idle");
    }
  }
}

std::uint8_t ShmRingBridge::telemetryTick() noexcept {
  checkRegionOrphaned();

  auto& s = state_.get();
  auto& tlm = telemetry_.get();
  tlm.frames_published = s.frames_published;
  tlm.pushes_failed_full = s.pushes_failed_full;
  tlm.signals_failed = s.signals_failed;
  tlm.channel_open = s.channel_open;
  tlm.source_resolved = s.source_resolved;
  tlm.region_orphaned = s.region_orphaned;

  auto* log = componentLog();
  if (log != nullptr) {
    const auto& p = tunables_.get();
    log->info(label(), fmt::format("tick={} app={:#x}v{} pub={} full={} sig_fail={} "
                                   "open={} resolved={} orphaned={} rx_cmds={}/{}/{}",
                                   s.tick_count, p.app_magic, p.app_version, s.frames_published,
                                   s.pushes_failed_full, s.signals_failed, s.channel_open,
                                   s.source_resolved, s.region_orphaned, s.cmds_received,
                                   s.cmds_decode_errors, s.cmds_dispatch_errors));
  }
  return 0u;
}

} // namespace support
} // namespace system_core
