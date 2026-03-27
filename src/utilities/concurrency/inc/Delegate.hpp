#ifndef APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
#define APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
/**
 * @file Delegate.hpp
 * @brief Allocation-free callable wrappers for task dispatch.
 *
 * Provides lightweight alternatives to std::function suitable for
 * real-time hot paths. No dynamic allocation, no type erasure overhead.
 * Just a function pointer and opaque context.
 *
 * Types:
 *   - DelegateU8: Fixed signature uint8_t(void*) for task dispatch
 *   - Delegate<Ret, Args...>: Templated for arbitrary signatures
 */

#include <cstdint>

namespace apex {
namespace concurrency {

/* ----------------------------- DelegateU8 ------------------------------ */

/**
 * @struct DelegateU8
 * @brief Allocation-free callable: uint8_t(void*) noexcept.
 *
 * Designed for task dispatch where heap allocation must be avoided.
 * The function returns a uint8_t status code (0 = success).
 *
 * @note RT-safe: No allocation, constexpr-compatible, trivially copyable.
 */
struct DelegateU8 {
  using Fn = std::uint8_t (*)(void*) noexcept;

  Fn fn{nullptr};
  void* ctx{nullptr};

  constexpr std::uint8_t operator()() const noexcept { return fn ? fn(ctx) : std::uint8_t{0}; }

  constexpr explicit operator bool() const noexcept { return fn != nullptr; }
};

/* ----------------------------- Delegate --------------------------------- */

/**
 * @struct Delegate
 * @brief Templated allocation-free callable: Ret(Args...) noexcept.
 *
 * Generic delegate for arbitrary function signatures. The function pointer
 * receives a void* context as its first argument, followed by the Args.
 *
 * Usage:
 * @code
 *   struct MyContext { double scale; };
 *
 *   double myFunc(void* ctx, double x) noexcept {
 *     auto* c = static_cast<MyContext*>(ctx);
 *     return x * c->scale;
 *   }
 *
 *   MyContext ctx{2.0};
 *   Delegate<double, double> d{myFunc, &ctx};
 *   double result = d(3.0);  // returns 6.0
 * @endcode
 *
 * @tparam Ret Return type.
 * @tparam Args Argument types (excluding the context pointer).
 *
 * @note RT-safe: No allocation, constexpr-compatible, trivially copyable.
 */
template <typename Ret, typename... Args> struct Delegate {
  using Fn = Ret (*)(void*, Args...) noexcept;

  Fn fn{nullptr};
  void* ctx{nullptr};

  /** @brief Default constructor (null delegate). */
  constexpr Delegate() noexcept = default;

  /** @brief Construct with function pointer and context. */
  constexpr Delegate(Fn f, void* c) noexcept : fn(f), ctx(c) {}

  /**
   * @brief Invoke the delegate.
   * @param args Arguments to pass to the function.
   * @return Result from fn, or default-constructed Ret if fn is null.
   * @note RT-safe: Direct function call.
   */
  constexpr Ret operator()(Args... args) const noexcept { return fn ? fn(ctx, args...) : Ret{}; }

  /**
   * @brief Check if delegate has a valid function.
   * @return True if fn is non-null.
   * @note RT-safe: Pointer comparison.
   */
  constexpr explicit operator bool() const noexcept { return fn != nullptr; }
};

/**
 * @brief Specialization for void return type.
 *
 * Does nothing if fn is null (no return value to synthesize).
 */
template <typename... Args> struct Delegate<void, Args...> {
  using Fn = void (*)(void*, Args...) noexcept;

  Fn fn{nullptr};
  void* ctx{nullptr};

  constexpr Delegate() noexcept = default;
  constexpr Delegate(Fn f, void* c) noexcept : fn(f), ctx(c) {}

  constexpr void operator()(Args... args) const noexcept {
    if (fn)
      fn(ctx, args...);
  }

  constexpr explicit operator bool() const noexcept { return fn != nullptr; }
};

/* -------------------------- Helper Functions ---------------------------- */

/**
 * @brief Create a Delegate from a function pointer and context.
 *
 * @tparam Ret Return type.
 * @tparam Args Argument types.
 * @param fn Function pointer.
 * @param ctx Context pointer.
 * @return Delegate wrapping the function and context.
 * @note RT-safe: No allocation.
 */
template <typename Ret, typename... Args>
constexpr Delegate<Ret, Args...> makeDelegate(Ret (*fn)(void*, Args...) noexcept,
                                              void* ctx) noexcept {
  return Delegate<Ret, Args...>{fn, ctx};
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
