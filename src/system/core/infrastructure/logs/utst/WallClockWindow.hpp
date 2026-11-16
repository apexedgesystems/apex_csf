#ifndef APEX_SYSTEM_LOGS_UTST_WALL_CLOCK_WINDOW_HPP
#define APEX_SYSTEM_LOGS_UTST_WALL_CLOCK_WINDOW_HPP
/**
 * @file WallClockWindow.hpp
 * @brief The wall-clock seconds a timestamp taken inside a stretch of a test
 *        can show, for tests that compare a whole timestamp or a name built
 *        from one.
 *
 * The wall clock can be set while a test runs, backward or forward by any
 * amount. A window reads the wall clock and the steady clock when it opens
 * and when it closes. When the two clocks disagree about how long the
 * stretch took, a timestamp taken inside it can be anywhere, so the window
 * yields nothing and the test repeats the stretch, a fixed number of times.
 * A stretch the clocks agree on yields a short list of seconds whose length
 * depends on how long the stretch took, never on how far the clock was set.
 *
 * Limit: two settings inside one stretch that cancel each other are not
 * seen, since both clocks are read at its ends only.
 */

#include <chrono>
#include <ctime>
#include <vector>

namespace logs {
namespace test {

/// How often a test repeats a stretch whose window yields nothing.
constexpr int WALL_CLOCK_ATTEMPTS = 5;

class WallClockWindow {
public:
  /// Longest stretch a window answers for. A longer one yields nothing.
  static constexpr std::chrono::seconds MAX_SPAN{10};

  /// Disagreement between the clocks that still counts as agreement. It also
  /// covers time(), which reads a second counter the kernel advances once
  /// per timer tick, so up to a tick behind the wall clock read here.
  static constexpr std::chrono::milliseconds SLACK{50};

  /// Opens the window.
  WallClockWindow() noexcept
      : outerStart_(Steady::now()), wallStart_(Wall::now()), innerStart_(Steady::now()) {}

  /**
   * @brief Closes the window.
   * @return The seconds since the epoch, in order, that a wall-clock reading
   *         taken since the window opened can show: at most 3 * MAX_SPAN in
   *         seconds plus two. Empty when the clocks disagree about the
   *         stretch or it outlasted MAX_SPAN.
   */
  [[nodiscard]] std::vector<std::time_t> close() const {
    // The wall-clock readings sit inside the outer pair of steady readings
    // and outside the inner pair, so an undisturbed wall clock measures a
    // length between the two steady lengths.
    const auto INNER_END = Steady::now();
    const auto WALL_END = Wall::now();
    const auto OUTER_END = Steady::now();
    const auto INNER = INNER_END - innerStart_;
    const auto OUTER = OUTER_END - outerStart_;
    const auto WALL = WALL_END - wallStart_;
    if (OUTER > MAX_SPAN || WALL < INNER - SLACK || WALL > OUTER + SLACK) {
      return {};
    }

    // A setting small enough to pass that comparison moved the clock by no
    // more than MARGIN, so the list reaches that far past both ends.
    const auto MARGIN = (OUTER - INNER) + SLACK;
    const auto FIRST = std::chrono::floor<std::chrono::seconds>(wallStart_ - MARGIN);
    const auto LAST = std::chrono::floor<std::chrono::seconds>(WALL_END + MARGIN);
    std::vector<std::time_t> seconds;
    for (auto t = FIRST; t <= LAST; t += std::chrono::seconds(1)) {
      seconds.push_back(Wall::to_time_t(t));
    }
    return seconds;
  }

private:
  using Steady = std::chrono::steady_clock;
  using Wall = std::chrono::system_clock;

  Steady::time_point outerStart_;
  Wall::time_point wallStart_;
  Steady::time_point innerStart_;
};

} // namespace test
} // namespace logs

#endif // APEX_SYSTEM_LOGS_UTST_WALL_CLOCK_WINDOW_HPP
