/**
 * @file SequenceCatalog.cpp
 * @brief SequenceCatalog implementation: filesystem scan and sorted lookup.
 */

#include "src/system/core/components/action/apex/inc/SequenceCatalog.hpp"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>

namespace system_core {
namespace data {

/* ----------------------------- SequenceCatalog Methods ----------------------------- */

std::size_t SequenceCatalog::scan(const std::filesystem::path& dir, SequenceType type) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
    return 0;
  }

  const char* EXT = (type == SequenceType::RTS) ? ".rts" : ".ats";
  std::size_t added = 0;

  for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto& ENTRY = *it;
    if (!ENTRY.is_regular_file(ec) || ENTRY.path().extension() != EXT) {
      continue;
    }

    if (count_ >= CATALOG_MAX_ENTRIES) {
      break;
    }

    // Read header first (8 bytes) to get stepCount, then read exact payload.
    std::uint8_t header[CatalogEntry::HEADER_SIZE]{};
    int fd = ::open(ENTRY.path().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    ssize_t bytesRead = ::read(fd, header, sizeof(header));
    if (bytesRead < static_cast<ssize_t>(sizeof(header))) {
      ::close(fd);
      continue;
    }

    std::uint16_t seqId = 0;
    std::uint16_t evtId = 0;
    std::memcpy(&seqId, &header[0], 2);
    std::memcpy(&evtId, &header[2], 2);
    const std::uint8_t STEP_COUNT = header[4];

    // Read full payload: header + steps (variable length)
    const std::size_t PAYLOAD_SIZE =
        CatalogEntry::HEADER_SIZE + static_cast<std::size_t>(STEP_COUNT) * CatalogEntry::STEP_SIZE;
    std::vector<std::uint8_t> fileData(PAYLOAD_SIZE, 0);
    std::memcpy(fileData.data(), header, sizeof(header));

    // Read remaining step data after header
    if (STEP_COUNT > 0) {
      const std::size_t STEP_BYTES = static_cast<std::size_t>(STEP_COUNT) * CatalogEntry::STEP_SIZE;
      ssize_t stepBytesRead = ::read(fd, fileData.data() + CatalogEntry::HEADER_SIZE, STEP_BYTES);
      if (stepBytesRead < static_cast<ssize_t>(STEP_BYTES)) {
        ::close(fd);
        continue;
      }
    }
    ::close(fd);

    // Check for duplicate ID
    if (findById(seqId) != nullptr) {
      continue;
    }

    auto& cat = entries_[count_];
    cat.sequenceId = seqId;
    cat.eventId = evtId;
    cat.stepCount = fileData[4];
    cat.armed = (fileData[7] != 0);
    cat.type = type;
    cat.priority = 0; // Default priority (can be overridden by TPRM)
    cat.blockCount = 0;

    // Cache the variable-length binary for RT loading (no filesystem I/O at trigger time)
    cat.binary = std::move(fileData);
    cat.binaryLoaded = true;

    // Store filename
    const std::string STEM = ENTRY.path().filename().string();
    std::snprintf(cat.filename, CATALOG_FILENAME_MAX, "%s", STEM.c_str());
    cat.absolutePath = ENTRY.path();

    ++count_;
    if (type == SequenceType::RTS) {
      ++rtsCount_;
    } else {
      ++atsCount_;
    }
    ++added;
  }

  // Re-sort for binary search
  if (added > 0) {
    sortById();
  }

  return added;
}

bool SequenceCatalog::add(const CatalogEntry& entry) noexcept {
  if (count_ >= CATALOG_MAX_ENTRIES) {
    return false;
  }

  // Duplicate check
  if (findById(entry.sequenceId) != nullptr) {
    return false;
  }

  entries_[count_] = entry;
  ++count_;

  if (entry.type == SequenceType::RTS) {
    ++rtsCount_;
  } else {
    ++atsCount_;
  }

  sortById();
  return true;
}

CatalogEntry* SequenceCatalog::findByIdMut(std::uint16_t sequenceId) noexcept {
  return const_cast<CatalogEntry*>(static_cast<const SequenceCatalog*>(this)->findById(sequenceId));
}

const CatalogEntry* SequenceCatalog::findById(std::uint16_t sequenceId) const noexcept {
  if (count_ == 0) {
    return nullptr;
  }

  // Binary search on sorted entries
  std::size_t lo = 0;
  std::size_t hi = count_;
  while (lo < hi) {
    const std::size_t MID = lo + (hi - lo) / 2;
    if (entries_[MID].sequenceId == sequenceId) {
      return &entries_[MID];
    }
    if (entries_[MID].sequenceId < sequenceId) {
      lo = MID + 1;
    } else {
      hi = MID;
    }
  }
  return nullptr;
}

void SequenceCatalog::sortById() noexcept {
  std::sort(entries_, entries_ + count_, [](const CatalogEntry& a, const CatalogEntry& b) {
    return a.sequenceId < b.sequenceId;
  });
}

} // namespace data
} // namespace system_core
