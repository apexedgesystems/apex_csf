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
/// format carries none or the header cannot be read. For formats that
/// define a spec hash, zero means "no generating spec" (a converted or
/// hand-assembled file): identical records to a generated artifact
/// but no identity a consumer pin can bind to, so pack refuses such an
/// entry unless the operator passes --allow-unspecified.
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

/// Roles whose inner format defines a spec hash (gravity tables record
/// zero until their header lands, so they are exempt).
bool roleDefinesSpecHash(WorldEntryRole role) {
  return role == WorldEntryRole::ATMOSPHERE || role == WorldEntryRole::TERRAIN;
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
  for (const auto& e : r.entries()) {
    std::printf("  entry role=%-10s suffix=%-7s size=%-10llu crc32=0x%08x specHash=0x%016llx\n",
                std::string(worldRoleName(e.role)).c_str(), e.suffix,
                static_cast<unsigned long long>(e.size), e.crc32,
                static_cast<unsigned long long>(e.specHash));
  }
  std::printf("verify: OK\n");
  return 0;
}

void usage() {
  std::fprintf(stderr,
               "usage: world_pack --out <bundle> --body <name> --uid <hex componentId>\n"
               "                  [--kind <kind>]  (default: world)\n"
               "                  --entry <role>=<file> [--entry <role>=<file> ...]\n"
               "                  [--allow-unspecified]  (pack entries whose spec_hash is 0)\n"
               "       world_pack --verify <bundle>\n"
               "roles: registered vocabulary (gravity, terrain, atmosphere)\n");
}

} // namespace

int main(int argc, char** argv) {
  std::string out;
  std::string body;
  std::uint32_t componentId = 0;
  const BundleKindInfo* kind = bundleKindByName("world"); // default kind
  std::vector<WorldEntrySource> sources;
  bool allowUnspecified = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view A{argv[i]};
    const bool HAS_VALUE = i + 1 < argc;
    if (A == "--verify" && HAS_VALUE) {
      return verify(argv[i + 1]);
    }
    if (A == "--allow-unspecified") {
      allowUnspecified = true;
      continue;
    }
    if (!HAS_VALUE) {
      usage();
      return 2;
    }
    const char* V = argv[++i];
    if (A == "--out") {
      out = V;
    } else if (A == "--kind") {
      kind = bundleKindByName(V);
      if (kind == nullptr) {
        std::fprintf(stderr, "world_pack: unknown bundle kind %s\n", V);
        return 2;
      }
    } else if (A == "--body") {
      body = V;
    } else if (A == "--uid") {
      componentId = static_cast<std::uint32_t>(std::strtoul(V, nullptr, 16));
    } else if (A == "--entry") {
      // role=path against the registered vocabulary; the suffix rides
      // from the same table, so manifests never state formats.
      const std::string_view SPEC{V};
      const std::size_t EQ = SPEC.find('=');
      WorldRoleInfo info{};
      if (EQ == std::string_view::npos || kind == nullptr ||
          !bundleRoleFromName(*kind, SPEC.substr(0, EQ), info)) {
        std::fprintf(stderr, "world_pack: role in --entry %s is not in the %s vocabulary\n", V,
                     kind != nullptr ? std::string(kind->name).c_str() : "?");
        return 2;
      }
      const std::string PATH{SPEC.substr(EQ + 1)};
      const std::uint64_t SPEC_HASH = sniffSpecHash(PATH, info.role);
      if (SPEC_HASH == 0 && roleDefinesSpecHash(info.role) && !allowUnspecified) {
        std::fprintf(stderr,
                     "world_pack: %s carries spec_hash 0 (no generating spec: a converted or "
                     "hand-assembled file) -- a consumer pin cannot bind to it. Regenerate the "
                     "artifact from its spec, or pass --allow-unspecified to pack it knowingly.\n",
                     PATH.c_str());
        return 2;
      }
      sources.push_back({info.role, info.suffix, PATH, SPEC_HASH});
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
  if (kind == nullptr || componentId < kind->componentIdFirst ||
      componentId > kind->componentIdLast) {
    std::fprintf(stderr, "world_pack: uid 0x%04x outside the %s kind range [0x%04x, 0x%04x]\n",
                 componentId, kind != nullptr ? std::string(kind->name).c_str() : "?",
                 kind != nullptr ? kind->componentIdFirst : 0,
                 kind != nullptr ? kind->componentIdLast : 0);
    return 2;
  }
  const WorldBundleCheck RC = WorldBundleWriter::write(out, UID, body, sources);
  if (RC != WorldBundleCheck::OK) {
    std::fprintf(stderr, "world_pack: pack failed (%s): %s\n", toString(RC), out.c_str());
    return 1;
  }
  return verify(out.c_str());
}
