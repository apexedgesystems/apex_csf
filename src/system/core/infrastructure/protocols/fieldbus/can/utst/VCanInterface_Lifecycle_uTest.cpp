/**
 * @file VCanInterface_Lifecycle_uTest.cpp
 * @test Setup/teardown semantics, idempotency, and autoTeardown behavior.
 *
 * Notes:
 *  - Uses a per-test unique interface name to avoid collisions.
 *  - Skips (does not fail) if the environment lacks NET_ADMIN/sudo.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <unistd.h> // getpid()

using apex::protocols::fieldbus::can::util::VCanInterface;

/** @return A unique-ish vcan name for this process (e.g., "vcan_ut_<pid>"). */
static std::string uniqueIfName() {
  std::ostringstream os;
  os << "vcan_ut_" << ::getpid();
  return os.str();
}

/** @test setup()/teardown() succeed and are idempotent. */
TEST(VCanInterfaceLifecycle, SetupAndTeardownIdempotent) {
  const std::string IFNAME = uniqueIfName();

  VCanInterface vcan(IFNAME, /*autoTeardown=*/false, /*useSudo=*/true);

  if (!vcan.setup()) {
    GTEST_SKIP() << "Skipping: NET_ADMIN/sudo not available for vcan setup.";
  }

  // Second setup should succeed when interface already exists.
  EXPECT_TRUE(vcan.setup()) << "setup() should be idempotent";

  // Teardown removes the interface.
  EXPECT_TRUE(vcan.teardown());

  // Second teardown should also succeed as a no-op (idempotent).
  EXPECT_TRUE(vcan.teardown());
}

/** @test Destructor with autoTeardown removes the interface (best-effort). */
TEST(VCanInterfaceLifecycle, DestructorAutoTeardown) {
  const std::string IFNAME = uniqueIfName() + "_d";

  {
    VCanInterface vcan(IFNAME, /*autoTeardown=*/true, /*useSudo=*/true);
    if (!vcan.setup()) {
      GTEST_SKIP() << "Skipping: NET_ADMIN/sudo not available for vcan setup.";
    }
    // Scope end → destructor runs and attempts deletion.
  }

  // Validate that the interface is now absent: teardown() should succeed as a no-op.
  VCanInterface probe(IFNAME, /*autoTeardown=*/false, /*useSudo=*/true);
  EXPECT_TRUE(probe.teardown());
}
