#ifndef APEX_MATH_FRAMES_FRAME_GRAPH_HPP
#define APEX_MATH_FRAMES_FRAME_GRAPH_HPP
/**
 * @file FrameGraph.hpp
 * @brief Fixed-capacity frame forest: one edge per frame, any-to-any resolve.
 *
 * Every frame is defined by exactly one edge -- its Transform to a parent --
 * so N frames cost N definitions and any-to-any conversion falls out of
 * resolve(from, to, t): compose up from `from` to the common ancestor, then
 * down into `to`. Multiple roots are first-class (an Earth-inertial tree and
 * a Moon-inertial tree can coexist); resolving across disconnected trees
 * reports ERROR_NO_PATH.
 *
 * Edge kinds (the whole taxonomy):
 *  - STATIC       stored Transform, set once (mounts, site frames);
 *                 updatable for slow reconfiguration.
 *  - TIME_DRIVEN  provider evaluated with the explicit sim time t (never
 *                 wall clock): earth rotation, lunar rotation.
 *  - STATE_DRIVEN provider fed by live state through its context pointer
 *                 (6DOF pose, moving CG); t is passed through for providers
 *                 that also interpolate.
 *
 * Providers use the house Delegate (function pointer + context) -- never
 * std::function -- and write a child-to-parent Transform. Names are for
 * logging only and never touch the resolve path.
 *
 * Storage is a compile-time-capacity node table; FrameIds are indices into
 * it. No heap, no pointers on the resolve path, resolve depth bounded by
 * capacity.
 *
 * @note RT-SAFE: All operations noexcept, no allocation.
 */

#include "src/utilities/concurrency/mcu/inc/Delegate.hpp"
#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

/* -------------------------------- FrameId --------------------------------- */

/** @brief Index handle into a FrameGraph's node table. */
using FrameId = uint16_t;

/** @brief Sentinel: no frame (a root's parent; a failed add). */
inline constexpr FrameId K_NO_FRAME = 0xFFFF;

/** @brief Edge taxonomy (introspection only; resolve treats provider kinds alike). */
enum class EdgeKind : uint8_t {
  STATIC = 0,
  TIME_DRIVEN = 1,
  STATE_DRIVEN = 2,
  ROOT = 3 ///< no edge: the frame is a tree root
};

/* ------------------------------- FrameGraph ------------------------------- */

/**
 * @brief The owner of all frame material: the registry, every edge, and the
 *        resolve machinery. Apps configure it once (sites, mounts, epoch
 *        material lives in the catalog layer) and query it per tick.
 *
 * @tparam T        Element type (float or double).
 * @tparam CAPACITY Compile-time maximum frame count.
 */
template <typename T, size_t CAPACITY = 32> class FrameGraph {
  static_assert(CAPACITY >= 1 && CAPACITY < K_NO_FRAME, "capacity must fit FrameId");

public:
  /// Edge provider: writes the child-to-parent Transform for sim time t.
  /// Context is the provider's own state (a 6DOF state block, an epoch).
  using EdgeProvider = concurrency::Delegate<uint8_t, T, Transform<T>*>;

  FrameGraph() noexcept = default;

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Add a tree root (no parent). */
  uint8_t addRoot(const char* name, FrameId& out) noexcept {
    return addNode(K_NO_FRAME, EdgeKind::ROOT, name, out);
  }

  /** @brief Add a frame with a stored child-to-parent transform. */
  uint8_t addStatic(FrameId parent, const Transform<T>& edge, const char* name,
                    FrameId& out) noexcept {
    const uint8_t RC = addNode(parent, EdgeKind::STATIC, name, out);
    if (RC != 0) {
      return RC;
    }
    nodes_[out].edge = edge;
    return RC;
  }

  /** @brief Add a frame whose edge is evaluated from the explicit sim time. */
  uint8_t addTimeDriven(FrameId parent, EdgeProvider provider, const char* name,
                        FrameId& out) noexcept {
    return addProvided(parent, EdgeKind::TIME_DRIVEN, provider, name, out);
  }

  /** @brief Add a frame whose edge is fed by live state via the provider ctx. */
  uint8_t addStateDriven(FrameId parent, EdgeProvider provider, const char* name,
                         FrameId& out) noexcept {
    return addProvided(parent, EdgeKind::STATE_DRIVEN, provider, name, out);
  }

  /** @brief Update a STATIC frame's stored edge (slow reconfiguration). */
  uint8_t updateStatic(FrameId id, const Transform<T>& edge) noexcept {
    if (id >= count_ || nodes_[id].kind != EdgeKind::STATIC) {
      return static_cast<uint8_t>(Status::ERROR_BAD_FRAME);
    }
    nodes_[id].edge = edge;
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  /* ------------------------------ Introspection --------------------------- */

  [[nodiscard]] size_t size() const noexcept { return count_; }
  [[nodiscard]] bool valid(FrameId id) const noexcept { return id < count_; }
  [[nodiscard]] FrameId parentOf(FrameId id) const noexcept {
    return id < count_ ? nodes_[id].parent : K_NO_FRAME;
  }
  [[nodiscard]] EdgeKind kindOf(FrameId id) const noexcept {
    return id < count_ ? nodes_[id].kind : EdgeKind::ROOT;
  }
  /** @brief Logging only -- never on the resolve path. */
  [[nodiscard]] const char* nameOf(FrameId id) const noexcept {
    return id < count_ ? nodes_[id].name : "?";
  }

  /* -------------------------------- Resolve ------------------------------- */

  /**
   * @brief out = the transform mapping `from` coordinates into `to`
   *        coordinates at sim time t (compose up to the common ancestor of
   *        both frames, then down).
   */
  uint8_t resolve(FrameId from, FrameId to, T t, Transform<T>& out) const noexcept {
    if (from >= count_ || to >= count_) {
      return static_cast<uint8_t>(Status::ERROR_BAD_FRAME);
    }

    // Ancestor chains, leaf first. Depth is bounded by capacity.
    FrameId upChain[CAPACITY];
    FrameId downChain[CAPACITY];
    size_t upLen = 0, downLen = 0;
    for (FrameId f = from; f != K_NO_FRAME; f = nodes_[f].parent) {
      upChain[upLen++] = f;
    }
    for (FrameId f = to; f != K_NO_FRAME; f = nodes_[f].parent) {
      downChain[downLen++] = f;
    }
    // Disconnected trees: different roots.
    if (upChain[upLen - 1] != downChain[downLen - 1]) {
      return static_cast<uint8_t>(Status::ERROR_NO_PATH);
    }
    // Trim the shared tail (common ancestors) down to the deepest one.
    while (upLen > 1 && downLen > 1 && upChain[upLen - 2] == downChain[downLen - 2]) {
      --upLen;
      --downLen;
    }
    // upChain[upLen-1] == downChain[downLen-1] == deepest common ancestor.

    // X(ancestor <- from): compose the child->parent edges walking up.
    Transform<T> acc; // identity
    Transform<T> edge, tmp;
    for (size_t i = 0; i + 1 < upLen; ++i) { // exclude the ancestor itself
      uint8_t rc = edgeOf(upChain[i], t, edge);
      if (rc != 0) {
        return rc;
      }
      // Applying up-edges leaf-outward: acc = edge_top ... edge_from.
      composeInto(edge, acc, tmp);
      acc = tmp;
    }

    // X(ancestor <- to), then invert it onto the accumulator.
    Transform<T> down; // identity
    for (size_t i = 0; i + 1 < downLen; ++i) {
      uint8_t rc = edgeOf(downChain[i], t, edge);
      if (rc != 0) {
        return rc;
      }
      composeInto(edge, down, tmp);
      down = tmp;
    }
    Transform<T> downInv;
    (void)inverseInto(down, downInv);
    composeInto(downInv, acc, out);
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  /* ----------------------------- Fluent sugar ----------------------------- */

  /** @brief Query proxy: `g.in(to).from(a).point(p, out, t)` reads as prose
   *         and keeps the point/vector split at the call site. */
  class Query {
  public:
    Query(const FrameGraph* g, FrameId to, FrameId from) noexcept : g_(g), to_(to), from_(from) {}

    /** @brief Position: lever arms apply along the whole path. */
    uint8_t point(const T* pIn, T* out, T t) const noexcept {
      Transform<T> x;
      const uint8_t RC = g_->resolve(from_, to_, t, x);
      return RC != 0 ? RC : transformPointInto(x, pIn, out);
    }

    /** @brief Direction: rotation only. */
    uint8_t vector(const T* vIn, T* out, T t) const noexcept {
      Transform<T> x;
      const uint8_t RC = g_->resolve(from_, to_, t, x);
      return RC != 0 ? RC : rotateVectorInto(x, vIn, out);
    }

  private:
    const FrameGraph* g_;
    FrameId to_;
    FrameId from_;
  };

  class QueryTarget {
  public:
    QueryTarget(const FrameGraph* g, FrameId to) noexcept : g_(g), to_(to) {}
    [[nodiscard]] Query from(FrameId f) noexcept { return Query(g_, to_, f); }

  private:
    const FrameGraph* g_;
    FrameId to_;
  };

  /** @brief "expressed in `to`": start of the fluent query. */
  [[nodiscard]] QueryTarget in(FrameId to) const noexcept { return QueryTarget(this, to); }

private:
  struct Node {
    Transform<T> edge{};     ///< STATIC storage (identity otherwise)
    EdgeProvider provider{}; ///< TIME/STATE_DRIVEN evaluator
    FrameId parent{K_NO_FRAME};
    EdgeKind kind{EdgeKind::ROOT};
    const char* name{""};
  };

  uint8_t addNode(FrameId parent, EdgeKind kind, const char* name, FrameId& out) noexcept {
    if (count_ >= CAPACITY) {
      return static_cast<uint8_t>(Status::ERROR_CAPACITY);
    }
    if (kind != EdgeKind::ROOT && parent >= count_) {
      return static_cast<uint8_t>(Status::ERROR_BAD_FRAME);
    }
    out = static_cast<FrameId>(count_);
    nodes_[count_].parent = kind == EdgeKind::ROOT ? K_NO_FRAME : parent;
    nodes_[count_].kind = kind;
    nodes_[count_].name = name != nullptr ? name : "";
    ++count_;
    return static_cast<uint8_t>(Status::SUCCESS);
  }

  uint8_t addProvided(FrameId parent, EdgeKind kind, EdgeProvider provider, const char* name,
                      FrameId& out) noexcept {
    if (!provider) {
      return static_cast<uint8_t>(Status::ERROR_NO_PROVIDER);
    }
    const uint8_t RC = addNode(parent, kind, name, out);
    if (RC != 0) {
      return RC;
    }
    nodes_[out].provider = provider;
    return RC;
  }

  /** @brief The child-to-parent edge of `id` at time t. */
  uint8_t edgeOf(FrameId id, T t, Transform<T>& out) const noexcept {
    const Node& n = nodes_[id];
    if (n.kind == EdgeKind::STATIC) {
      out = n.edge;
      return static_cast<uint8_t>(Status::SUCCESS);
    }
    return n.provider(t, &out);
  }

  Node nodes_[CAPACITY];
  size_t count_ = 0;
};

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_FRAME_GRAPH_HPP
