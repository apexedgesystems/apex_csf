#ifndef APEX_SIM_SENSORS_GAUSSIAN_SAMPLER_HPP
#define APEX_SIM_SENSORS_GAUSSIAN_SAMPLER_HPP
/**
 * @file GaussianSampler.hpp
 * @brief Deterministic, portable standard-normal sampler for sensor noise.
 *
 * Sensor noise must reproduce bit-for-bit across toolchains and targets so that
 * replay, Monte Carlo, and assurance runs are deterministic. std::normal_distribution
 * is implementation-defined -- the same engine and seed yield different sequences
 * on libstdc++ vs libc++ -- so it cannot provide that guarantee. This sampler
 * instead pairs the standardized std::mt19937 engine (whose output sequence is
 * fixed by the standard) with an explicit Box-Muller transform built only from
 * that output and basic arithmetic. Given a seed, the normal sequence is identical
 * on every platform.
 *
 * Box-Muller produces two independent normals per pair of uniforms; the second is
 * cached and returned on the next call. Reseeding discards the cached value so a
 * given seed always reproduces the same sequence.
 */

#include <cmath>
#include <cstdint>
#include <random>

namespace sim::sensors {

/* ----------------------------- GaussianSampler ----------------------------- */

class GaussianSampler {
public:
  explicit GaussianSampler(std::uint32_t seed = 0u) noexcept : gen_(seed) {}

  /** @brief Draw one standard-normal N(0, 1) sample. */
  [[nodiscard]] double gaussian() noexcept {
    if (has_cached_) {
      has_cached_ = false;
      return cached_;
    }
    // Two uniforms -> two independent normals (cache the second).
    const double u1 = (static_cast<double>(gen_()) + 1.0) * kInv2Pow32Plus1; // (0, 1]
    const double u2 = static_cast<double>(gen_()) * kInv2Pow32;              // [0, 1)
    const double r = std::sqrt(-2.0 * std::log(u1));
    const double theta = kTwoPi * u2;
    cached_ = r * std::sin(theta);
    has_cached_ = true;
    return r * std::cos(theta);
  }

  /** @brief Draw N(mean, sigma). */
  [[nodiscard]] double gaussian(double mean, double sigma) noexcept {
    return mean + sigma * gaussian();
  }

  /** @brief Reseed the engine and discard any cached sample. */
  void seed(std::uint32_t s) noexcept {
    gen_.seed(s);
    has_cached_ = false;
  }

private:
  static constexpr double kTwoPi = 6.283185307179586;
  static constexpr double kInv2Pow32 = 1.0 / 4294967296.0;      // 1 / 2^32      -> [0, 1)
  static constexpr double kInv2Pow32Plus1 = 1.0 / 4294967297.0; // 1 / (2^32 + 1) -> (0, 1]

  std::mt19937 gen_;
  double cached_ = 0.0;
  bool has_cached_ = false;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_GAUSSIAN_SAMPLER_HPP
