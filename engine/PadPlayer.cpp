#include "engine/PadPlayer.h"

#include <algorithm>
#include <cmath>

namespace vs {

std::unique_ptr<TrackBuffer> makeSynthPad(int key) {
    constexpr double T = 6.0;  // segundos; todas as parciais têm número inteiro de ciclos em T -> loop perfeito
    const int64_t frames = static_cast<int64_t>(T * kEngineSampleRate);
    auto t = std::make_unique<TrackBuffer>();
    t->frames = frames;
    t->pcm.assign(static_cast<size_t>(frames) * 2, 0.f);

    const double root = 130.8128 * std::pow(2.0, (key % 12) / 12.0);  // C3 + semitons
    struct Partial { double mult, amp; };
    const Partial parts[] = {{1.0, 1.0}, {1.5, 0.55}, {2.0, 0.6}, {3.0, 0.25}, {0.5, 0.5}};
    for (int ch = 0; ch < 2; ++ch) {
        const double detune = (ch == 0 ? -3.0 : 3.0) / 1200.0;  // ±3 cents: largura estéreo
        for (const auto& p : parts) {
            double f = root * p.mult * std::pow(2.0, detune);
            f = std::round(f * T) / T;                              // ciclos inteiros
            for (int pass = 0; pass < 2; ++pass) {                  // duas camadas levemente dessintonizadas
                const double ff = f + pass * (1.0 / T) * (ch == 0 ? 1 : 2);
                const double amp = p.amp * (pass == 0 ? 1.0 : 0.3);   // camada 2: só um "chorus" suave
                const double ph = 0.7 * ch + pass * 1.3 + p.mult;
                for (int64_t i = 0; i < frames; ++i)
                    t->pcm[i * 2 + ch] += static_cast<float>(amp * std::sin(2 * M_PI * ff * i / kEngineSampleRate + ph));
            }
        }
        // respiração lenta (0,5 Hz = 3 ciclos em 6 s) e normalização
        for (int64_t i = 0; i < frames; ++i) {
            const double lfo = 1.0 + 0.10 * std::sin(2 * M_PI * 3.0 * i / frames + 0.9 * ch);
            t->pcm[i * 2 + ch] = static_cast<float>(t->pcm[i * 2 + ch] * lfo * 0.09);
        }
    }
    return t;
}

void PadPlayer::mixLayer(Layer& ly, float* l, float* r, int n, float dir, float step, float gain) {
    const TrackBuffer* b = ly.key >= 0 ? buffers[ly.key].load(std::memory_order_acquire) : nullptr;
    if (!b || b->frames <= 0) { ly.level = dir > 0 ? ly.level : 0.f; return; }
    int64_t pos = ly.pos;
    float lv = ly.level;
    for (int i = 0; i < n; ++i) {
        lv = std::max(0.f, std::min(1.f, lv + dir * step));
        const float g = eqPower(lv) * gain;
        l[i] += b->pcm[pos * 2] * g;
        r[i] += b->pcm[pos * 2 + 1] * g;
        if (++pos >= b->frames) pos = 0;
    }
    ly.pos = pos;
    ly.level = lv;
}

void PadPlayer::render(float* l, float* r, int n) {
    const int req = requested_.load(std::memory_order_relaxed);
    if (req != target_) {
        target_ = req;
        if (req >= 0 && buffers[req].load(std::memory_order_acquire) == nullptr) {
            target_ = -1;  // pad ainda não carregado: ignora
        }
        if (target_ != cur_.key) {
            if (cur_.key >= 0 && cur_.level > 0.f) old_ = cur_;   // camada atual passa a sumir
            cur_ = Layer{target_, 0, 0.f};
            if (target_ < 0) cur_.key = -1;
        }
        published_.store(target_, std::memory_order_relaxed);
    }
    const float step = 1.0f / std::max(1.0f, fadeMs_.load(std::memory_order_relaxed) * 0.001f * kEngineSampleRate);
    const float gain = gain_.load(std::memory_order_relaxed);
    if (old_.key >= 0 && old_.level > 0.f) {
        mixLayer(old_, l, r, n, -1.f, step, gain);
        if (old_.level <= 0.f) old_.key = -1;
    }
    if (cur_.key >= 0) mixLayer(cur_, l, r, n, +1.f, step, gain);
}

}  // namespace vs
