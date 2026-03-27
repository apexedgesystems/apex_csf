#ifndef APEX_UTILITIES_COMPATIBILITY_CUDA_MEMORY_HPP
#define APEX_UTILITIES_COMPATIBILITY_CUDA_MEMORY_HPP

/**
 * @file compat_cuda_memory.hpp
 * @brief Optional RAII helpers for pinned host memory (H2D/D2H friendly).
 *
 * Safe to include in CPU-only builds. Pinned allocation is used only when BOTH:
 *   - CUDA runtime headers are available, and
 *   - COMPAT_CUDA_USE_RUNTIME is defined (enable via CMake)
 * Otherwise falls back to std::vector<T>.
 */

#include "compat_cuda_attrs.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

// Detect CUDA runtime header availability independently of device compilation.
#ifndef COMPAT_CUDA_HAS_RUNTIME_HEADERS
#if defined(__has_include)
#if __has_include(<cuda_runtime_api.h>)
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS 1
#else
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS 0
#endif
#else
#define COMPAT_CUDA_HAS_RUNTIME_HEADERS COMPAT_CUDA_AVAILABLE
#endif
#endif

#if COMPAT_CUDA_HAS_RUNTIME_HEADERS && defined(COMPAT_CUDA_USE_RUNTIME)
#include <cuda_runtime_api.h>
#endif

namespace apex {
namespace compat {
namespace cuda {

/* --------------------------------- Types ---------------------------------- */
template <typename T> class pinned_buffer {
  static_assert(!std::is_const<T>::value, "pinned_buffer<T>: T must be non-const");
  // Trivially copyable is strongly preferred for DMA-friendly buffers, but not required.
  // static_assert(std::is_trivially_copyable<T>::value, "T should be trivially copyable");

public:
  pinned_buffer() = default;

  explicit pinned_buffer(std::size_t count) { allocate(count); }

  ~pinned_buffer() { release(); }

  pinned_buffer(const pinned_buffer&) = delete;
  pinned_buffer& operator=(const pinned_buffer&) = delete;

  pinned_buffer(pinned_buffer&& other) noexcept { move_from(std::move(other)); }
  pinned_buffer& operator=(pinned_buffer&& other) noexcept {
    if (this != &other) {
      release();
      move_from(std::move(other));
    }
    return *this;
  }

  // Allocate or reallocate to `count` elements. Contents are not preserved.
  void allocate(std::size_t count) {
    release();
    size_ = count;

#if COMPAT_CUDA_HAS_RUNTIME_HEADERS && defined(COMPAT_CUDA_USE_RUNTIME)
    if (count > 0) {
      T* ptr = nullptr;
      if (cudaMallocHost(reinterpret_cast<void**>(&ptr), count * sizeof(T)) == cudaSuccess && ptr) {
        data_ = ptr;
        pinned_ = true;
        // Ensure fallback vector is empty when pinned.
        vec_.clear();
        vec_.shrink_to_fit();
        return;
      }
      // Fallback to vector on failure.
      vec_.resize(count);
      data_ = vec_.data();
      pinned_ = false;
    }
    // count == 0: keep empty state
#else
    if (count > 0) {
      vec_.resize(count);
      data_ = vec_.data();
      pinned_ = false;
    }
#endif
  }

  // Resize (re-allocate). Drops current contents.
  void resize(std::size_t count) { allocate(count); }

  // Release any allocation; leaves the buffer empty.
  void release() noexcept {
#if COMPAT_CUDA_HAS_RUNTIME_HEADERS && defined(COMPAT_CUDA_USE_RUNTIME)
    if (pinned_ && data_) {
      (void)cudaFreeHost(data_);
    }
#endif
    data_ = nullptr;
    size_ = 0;
    pinned_ = false;
    vec_.clear();
    vec_.shrink_to_fit();
  }

  // Clear to size==0, preserving pinned mode (no-op vs release? keep simple: release).
  void clear() noexcept { release(); }

  // Swap with another buffer.
  void swap(pinned_buffer& other) noexcept {
    using std::swap;
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(pinned_, other.pinned_);
    swap(vec_, other.vec_);
  }

  // Observers
  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  bool is_pinned() const noexcept { return pinned_; }

  // Element access
  T& operator[](std::size_t i) noexcept { return data_[i]; }
  const T& operator[](std::size_t i) const noexcept { return data_[i]; }

private:
  void move_from(pinned_buffer&& o) noexcept {
    data_ = o.data_;
    o.data_ = nullptr;

    size_ = o.size_;
    o.size_ = 0;

    pinned_ = o.pinned_;
    o.pinned_ = false;

    vec_ = std::move(o.vec_);
  }

  T* data_ = nullptr;
  std::size_t size_ = 0;
  bool pinned_ = false;
  std::vector<T> vec_; // fallback storage when not pinned or on allocation failure
};

} // namespace cuda
} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_CUDA_MEMORY_HPP
