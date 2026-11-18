#pragma once
#include <atomic>
#include <cmath>
#include <memory>

#include "engine/Constants.h"
#include "engine/TrackBuffer.h"

namespace vs {

/** Gera um pad sintético (loop contínuo perfeito) na tonalidade `key` (0=C .. 11=B). Provisório p/ testes. */
std::unique_ptr<TrackBuffer> makeSynthPad(int key);

/**
 * Player paralelo de Ambient Pads: independe do transporte/timeline. 12 loops (um por tom),
 * troca de tom com crossfade de potência constante; liga/desliga com fade.
 * Thread de áudio: render(). Controle: select/setGain/setFadeMs (atomics).
 */
class PadPlayer {
public:
    std::atomic<const TrackBuffer*> buffers[kNumPadKeys];

    PadPlayer() { for (auto& b : buffers) b.store(nullptr); }

    void select(int key) { requested_.store(key, std::memory_order_relaxed); }  // -1 = desligar
    void setGain(float g) { gain_.store(std::max(0.f, std::min(2.f, g)), std::memory_order_relaxed); }
    void setFadeMs(float ms) { fadeMs_.store(std::max(50.f, ms), std::memory_order_relaxed); }
    int activeKey() const { return published_.load(std::memory_order_relaxed); }

    /** Soma o pad (estéreo) em l/r. Thread de áudio. */
    void render(float* l, float* r, int n);

private:
    struct Layer { int key = -1; int64_t pos = 0; float level = 0.f; };
    static inline float eqPower(float level) { return std::sin(level * 1.5707963f); }
    void mixLayer(Layer& ly, float* l, float* r, int n, float dir, float step, float gain);

    std::atomic<int> requested_{-1};
    std::atomic<float> gain_{0.8f};
    std::atomic<float> fadeMs_{2500.f};
    std::atomic<int> published_{-1};
    int target_ = -1;
    Layer cur_, old_;
};

}  // namespace vs
