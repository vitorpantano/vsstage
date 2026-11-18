#pragma once
#include <atomic>

namespace vs {

// Máximo atômico lock-free para float (usado nos medidores de pico).
inline void atomicMax(std::atomic<float>& target, float value) {
    float cur = target.load(std::memory_order_relaxed);
    while (value > cur &&
           !target.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }
}

}  // namespace vs
