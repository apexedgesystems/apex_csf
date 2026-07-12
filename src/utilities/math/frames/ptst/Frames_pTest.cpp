/**
 * @file Frames_pTest.cpp
 * @brief Throughput baselines for the SE(3) transform operations (the
 *        per-hop costs a graph resolve pays).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/math/frames/inc/Catalog.hpp"
#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Geodetic.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

namespace fr = apex::math::frames;

PERF_TEST(FramesPerf, TransformPoint) {
  UB_PERF_GUARD(perf);
  fr::Transform<double> x;
  x.rotation().setFromEuler321(0.2, -0.3, 0.9);
  x.t[0] = 1.0;
  const double P[3] = {0.5, -1.5, 2.5};
  double out[3];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)fr::transformPointInto(x, P, out);
        sink = sink + out[0];
      },
      "frames_transform_point");
  std::printf("\n[frames] transformPoint: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(FramesPerf, Compose) {
  UB_PERF_GUARD(perf);
  fr::Transform<double> a, b, out;
  a.rotation().setFromEuler321(0.2, -0.3, 0.9);
  a.t[0] = 1.0;
  b.rotation().setFromAngleAxis(0.7, 0.0, 1.0, 0.0);
  b.t[1] = 4.0;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)fr::composeInto(a, b, out);
        sink = sink + out.t[0];
      },
      "frames_compose");
  std::printf("\n[frames] compose: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}
PERF_TEST(FramesPerf, ResolveFourHops) {
  UB_PERF_GUARD(perf);
  // sensor -> mount -> body -> site -> root: the control-consumer chain shape.
  fr::FrameGraph<double> g;
  fr::FrameId root = 0, site = 0, body = 0, mount = 0, sensor = 0;
  (void)g.addRoot("root", root);
  fr::Transform<double> e;
  e.t[0] = 1000.0;
  (void)g.addStatic(root, e, "site", site);
  e = fr::Transform<double>{};
  e.rotation().setFromEuler321(0.1, -0.2, 0.9);
  e.t[2] = -12.0;
  (void)g.addStatic(site, e, "body", body);
  e = fr::Transform<double>{};
  e.t[1] = 1.5;
  (void)g.addStatic(body, e, "mount", mount);
  e = fr::Transform<double>{};
  e.rotation().setFromAngleAxis(0.3, 0.0, 1.0, 0.0);
  (void)g.addStatic(mount, e, "sensor", sensor);

  fr::Transform<double> x;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)g.resolve(sensor, root, 0.0, x);
        sink = sink + x.t[0];
      },
      "frames_resolve_4hop");
  std::printf("\n[frames] resolve 4-hop: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(FramesPerf, GeodeticRoundTrip) {
  UB_PERF_GUARD(perf);
  double ecef[3], lat = 0, lon = 0, h = 0;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        fr::geodeticToEcefInto(0.6, -2.1, 120.0, ecef);
        fr::ecefToGeodeticInto(ecef, lat, lon, h);
        sink = sink + h;
      },
      "frames_geodetic_round_trip");
  std::printf("\n[frames] geodetic round-trip: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(FramesPerf, CatalogEciFromSite) {
  UB_PERF_GUARD(perf);
  fr::Epoch epoch;
  epoch.init(apex::math::celestial::JD_J2000);
  fr::FrameGraph<double> g;
  fr::CatalogIds ids;
  (void)fr::buildCatalog(g, epoch, ids);
  fr::Transform<double> siteEdge;
  fr::enuSiteEdgeInto(0.7, -1.9, 350.0, siteEdge);
  fr::FrameId site = 0;
  (void)g.addStatic(ids.ecef, siteEdge, "site", site);

  const double P[3] = {10.0, -5.0, 2.0};
  double out[3];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)g.in(ids.eci).from(site).point(P, out, 120.0);
        sink = sink + out[0];
      },
      "frames_catalog_site_to_eci");
  std::printf("\n[frames] site->ECI (time-driven hop): %.0f ops/s (%.1f ns)\n",
              result.callsPerSecond, 1.0e9 / result.callsPerSecond);
}

PERF_MAIN()
