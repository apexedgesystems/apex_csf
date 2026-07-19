/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/utilities/math/frames/inc/Catalog.hpp"
#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Geodetic.hpp"
#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Mount.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

namespace fr = apex::math::frames;

float probe() {
  fr::Transform<float> a, b, ab, inv;
  a.rotation().setFromAngleAxis(0.5f, 0.0f, 0.0f, 1.0f);
  a.t[0] = 2.0f;
  (void)fr::composeInto(a, b, ab);
  (void)fr::inverseInto(ab, inv);
  const float P[3] = {1.0f, 0.0f, 0.0f};
  float p[3], v[3];
  (void)fr::transformPointInto(inv, P, p);
  (void)fr::rotateVectorInto(inv, P, v);
  static_assert(sizeof(fr::Transform<float>) == 28, "flat POD");
  fr::FrameGraph<float, 8> g;
  fr::FrameId root = 0, body = 0;
  (void)g.addRoot("root", root);
  (void)g.addStatic(root, a, "body", body);
  fr::Transform<float> x;
  (void)g.resolve(body, root, 0.0f, x);
  static fr::Epoch epoch; // provider ctx must outlive the graph
  epoch.init(apex::math::celestial::JD_J2000);
  fr::CatalogIds ids;
  (void)fr::buildCatalog(g, epoch, ids);
  fr::Transform<float> site;
  fr::enuSiteEdgeInto(0.5f, -1.0f, 100.0f, site);
  fr::Mount<float> pod;
  pod.leverArmM[1] = 0.5f;
  fr::FrameId sensor = 0;
  (void)fr::addMount(g, body, pod, "pod", sensor);
  return p[0] + v[1] + (fr::ok(fr::Status::SUCCESS) ? 1.0f : 0.0f);
}
