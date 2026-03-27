#ifndef APEX_SYSTEM_CORE_SCHEDULABLE_TASKBUILDER_HPP
#define APEX_SYSTEM_CORE_SCHEDULABLE_TASKBUILDER_HPP
/**
 * @file TaskBuilder.hpp
 * @brief Ergonomic helpers for SchedulableTask construction.
 *
 * Provides:
 *  - bindMember()       : Zero-cost member function binding
 *  - bindLambda()       : Stateless lambda binding
 *  - bindFreeFunction() : C-style function binding
 *
 * For task sequencing, see SequenceGroup.hpp.
 *
 * RT-Safety:
 *  - All binding functions are constexpr/inline (zero runtime cost)
 *  - No heap allocation in hot paths
 */

#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"

#include <cstdint>
#include <type_traits>

namespace system_core {
namespace schedulable {

namespace detail {

/**
 * @brief Trampoline for member function calls via DelegateU8.
 * @tparam T Object type
 * @tparam MemFn Member function pointer
 */
template <class T, std::uint8_t (T::*MemFn)()>
inline std::uint8_t memberTrampoline(void* ctx) noexcept {
  return (static_cast<T*>(ctx)->*MemFn)();
}

/**
 * @brief Trampoline for stateless lambda/functor calls via DelegateU8.
 * @tparam F Functor type (must be stateless and convertible to function pointer)
 */
template <typename F> inline std::uint8_t lambdaTrampoline(void* ctx) noexcept {
  auto fn = reinterpret_cast<std::uint8_t (*)()>(ctx);
  return fn();
}

} // namespace detail

/* ----------------------------- API ----------------------------- */

/**
 * @brief Bind a member function to a DelegateU8.
 * @tparam T Object type
 * @tparam MemFn Member function pointer (signature: uint8_t T::fn())
 * @param obj Pointer to object instance
 * @return DelegateU8 wrapping the member function call
 *
 * @note Zero-cost abstraction: compiles to identical code as manual trampoline.
 *
 * Example:
 * @code
 * struct MyTask {
 *   uint8_t process() { return 0; }
 * };
 * MyTask task;
 * auto delegate = bindMember<MyTask, &MyTask::process>(&task);
 * SchedulableTask schedTask(delegate, "process");
 * @endcode
 */
template <class T, std::uint8_t (T::*MemFn)()>
inline apex::concurrency::DelegateU8 bindMember(T* obj) noexcept {
  return apex::concurrency::DelegateU8{&detail::memberTrampoline<T, MemFn>,
                                       static_cast<void*>(obj)};
}

/**
 * @brief Bind a stateless lambda to a DelegateU8.
 * @tparam F Lambda type (must be stateless, convertible to function pointer)
 * @param fn Lambda expression
 * @return DelegateU8 wrapping the lambda call
 *
 * @note Only works with stateless lambdas (no captures).
 *       Capturing lambdas require std::function and heap allocation.
 *
 * Example:
 * @code
 * auto delegate = bindLambda([]() -> uint8_t { return 42; });
 * SchedulableTask task(delegate, "lambda_task");
 * @endcode
 */
template <typename F, typename = std::enable_if_t<std::is_convertible_v<F, std::uint8_t (*)()>>>
inline apex::concurrency::DelegateU8 bindLambda(F&& fn) noexcept {
  using FnPtr = std::uint8_t (*)();
  FnPtr fnptr = static_cast<FnPtr>(fn);
  return apex::concurrency::DelegateU8{&detail::lambdaTrampoline<F>,
                                       reinterpret_cast<void*>(fnptr)};
}

/**
 * @brief Bind a free function to a DelegateU8.
 * @param fn Function pointer (signature: uint8_t fn())
 * @return DelegateU8 wrapping the function call
 *
 * @note For functions requiring context, use a wrapper with void* parameter.
 *
 * Example:
 * @code
 * uint8_t myFunction() { return 0; }
 * auto delegate = bindFreeFunction(&myFunction);
 * SchedulableTask task(delegate, "my_function");
 * @endcode
 */
inline apex::concurrency::DelegateU8 bindFreeFunction(std::uint8_t (*fn)()) noexcept {
  auto wrapper = [](void* ctx) noexcept -> std::uint8_t {
    auto fnptr = reinterpret_cast<std::uint8_t (*)()>(ctx);
    return fnptr();
  };
  return apex::concurrency::DelegateU8{wrapper, reinterpret_cast<void*>(fn)};
}

} // namespace schedulable
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULABLE_TASKBUILDER_HPP