/**
 * @file WorldPack.cpp
 * @brief World bundle packer/verifier CLI.
 *
 * Pack mode assembles role-keyed artifacts into a <body>.world.tprm
 * and prints the content hash (the value a consumer tprm pins).
 * Verify mode re-opens a bundle, checks the full content hash, and
 * prints the identity block. The tool is a thin argument layer over
 * the library writer/reader, so producers can invoke it as a black
 * box without knowing the container format.
 *
 * Per-entry spec hashes are sniffed from the inner artifact headers
 * (.atm / .htile carry one; .grav is headerless and records zero
 * until the format grows its header).
 */

#include "src/sim/environment/atmosphere/inc/Atm.hpp"
#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/world/inc/WorldBundle.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sim::environment::world;

/// Read the inner artifact's self-declared spec hash; zero when the
/// format carries none (or the header cannot be read -- pack still
/// succeeds; provenance is the artifact's own concern).
std::uint64_t sniffSpecHash(const std::filesystem::path& p, WorldEntryRole role) {
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (f == nullptr) {
    return 0;
  }
  std::uint64_t hash = 0;
  if (role == WorldEntryRole::ATMOSPHERE) {
    sim::environment::atmosphere::AtmHeader h{};
    if (std::fread(&h, 1, sizeof(h), f) == sizeof(h) &&
        std::memcmp(h.magic, sim::environment::atmosphere::kAtmMagic, 4) == 0) {
      hash = h.spec_hash;
    }
  } else if (role == WorldEntryRole::TERRAIN) {
    sim::environment::terrain::HtileHeader h{};
    if (std::fread(&h, 1, sizeof(h), f) == sizeof(h) && std::memcmp(h.magic, "HTIL", 4) == 0) {
      hash = h.spec_hash;
    }
  }
  std::fclose(f);
  return hash;
}

int verify(const char* path) {
  WorldBundleReader r;
  const WorldBundleCheck OPEN = r.open(path);
  if (OPEN != WorldBundleCheck::OK) {
    std::fprintf(stderr, "world_pack: open failed (%s): %s\n", toString(OPEN), path);
    return 1;
  }
  const WorldBundleCheck HASH = r.verifyContentHash();
  if (HASH != WorldBundleCheck::OK) {
    std::fprintf(stderr, "world_pack: content hash FAILED (%s): %s\n", toString(HASH), path);
    return 1;
  }
  const auto& H = r.header();
  std::printf("bundle: %s\n", path);
  std::printf("  body=%s uid=0x%06x version=%u entries=%u\n", H.body, H.fullUid,
              static_cast<unsigned>(H.version), static_cast<unsigned>(H.entryCount));
  std::printf("  contentHash=0x%016llx totalSize=%llu\n",
              static_cast<unsigned long long>(H.bundleContentHash),
              static_cast<unsigned long long>(H.totalSize));
  static constexpr const char* K_ROLES[] = {"gravity", "terrain", "atmosphere"};
  for (const auto& e : r.entries()) {
    const char* ROLE = e.role < 3 ? K_ROLES[e.role] : "?";
    std::printf("  entry role=%-10s suffix=%-7s size=%-10llu crc32=0x%08x specHash=0x%016llx\n",
                ROLE, e.suffix, static_cast<unsigned long long>(e.size), e.crc32,
                static_cast<unsigned long long>(e.specHash));
  }
  std::printf("verify: OK\n");
  return 0;
}

void usage() {
  std::fprintf(stderr,
               "usage: world_pack --out <bundle> --body <name> --uid <hex componentId>\n"
               "                  [--gravity <file>] [--terrain <file>] [--atmosphere <file>]\n"
               "       world_pack --verify <bundle>\n");
}

} // namespace

int main(int argc, char** argv) {
  std::string out;
  std::string body;
  std::uint32_t componentId = 0;
  std::vector<WorldEntrySource> sources;

  for (int i = 1; i < argc; ++i) {
    const std::string_view A{argv[i]};
    const bool HAS_VALUE = i + 1 < argc;
    if (A == "--verify" && HAS_VALUE) {
      return verify(argv[i + 1]);
    }
    if (!HAS_VALUE) {
      usage();
      return 2;
    }
    const char* V = argv[++i];
    if (A == "--out") {
      out = V;
    } else if (A == "--body") {
      body = V;
    } else if (A == "--uid") {
      componentId = static_cast<std::uint32_t>(std::strtoul(V, nullptr, 16));
    } else if (A == "--gravity") {
      sources.push_back({WorldEntryRole::GRAVITY, ".grav", V, 0});
    } else if (A == "--terrain") {
      sources.push_back(
          {WorldEntryRole::TERRAIN, ".htile", V, sniffSpecHash(V, WorldEntryRole::TERRAIN)});
    } else if (A == "--atmosphere") {
      sources.push_back(
          {WorldEntryRole::ATMOSPHERE, ".atm", V, sniffSpecHash(V, WorldEntryRole::ATMOSPHERE)});
    } else {
      usage();
      return 2;
    }
  }

  if (out.empty() || body.empty() || componentId == 0 || sources.empty()) {
    usage();
    return 2;
  }
  const std::uint32_t UID = worldFullUid(static_cast<std::uint16_t>(componentId));
  const WorldBundleCheck RC = WorldBundleWriter::write(out, UID, body, sources);
  if (RC != WorldBundleCheck::OK) {
    std::fprintf(stderr, "world_pack: pack failed (%s): %s\n", toString(RC), out.c_str());
    return 1;
  }
  return verify(out.c_str());
}
