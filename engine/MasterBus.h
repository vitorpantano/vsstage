#pragma once
#include <algorithm>
#include <cmath>

#include "engine/Constants.h"

namespace vs {

// ---------------------------------------------------------------------------------
// Biquad (RBJ cookbook), forma transposta II
// ---------------------------------------------------------------------------------
struct BiquadCoefs {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    bool bypass = true;

    enum Type { LowShelf, Peak, HighShelf };

    static BiquadCoefs make(Type t, double freq, double gainDb, double q, double sr) {
        BiquadCoefs c;
        if (std::fabs(gainDb) < 0.01) return c;  // 0 dB = bypass exato (transparente)
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * M_PI * freq / sr, cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        if (t == Peak) {
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        } else {
            const double beta = 2.0 * std::sqrt(A) * alpha;
            if (t == LowShelf) {
                b0 = A * ((A + 1) - (A - 1) * cw + beta);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - beta);
                a0 = (A + 1) + (A - 1) * cw + beta;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - beta;
            } else {
                b0 = A * ((A + 1) + (A - 1) * cw + beta);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - beta);
                a0 = (A + 1) - (A - 1) * cw + beta;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - beta;
            }
        }
        c.b0 = float(b0 / a0); c.b1 = float(b1 / a0); c.b2 = float(b2 / a0);
        c.a1 = float(a1 / a0); c.a2 = float(a2 / a0);
        c.bypass = false;
        return c;
    }
};

struct BiquadState {
    float z1 = 0, z2 = 0;
    inline float run(const BiquadCoefs& c, float x) {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

/** EQ gráfico de 3 bandas (graves 120 Hz shelf, médios 1 kHz peak, agudos 6 kHz shelf), estéreo. */
class Eq3 {
public:
    void setGains(float lowDb, float midDb, float highDb) {
        c_[0] = BiquadCoefs::make(BiquadCoefs::LowShelf, 120.0, lowDb, 0.707, kEngineSampleRate);
        c_[1] = BiquadCoefs::make(BiquadCoefs::Peak, 1000.0, midDb, 0.8, kEngineSampleRate);
        c_[2] = BiquadCoefs::make(BiquadCoefs::HighShelf, 6000.0, highDb, 0.707, kEngineSampleRate);
    }
    void process(float* l, float* r, int n) {
        for (int b = 0; b < 3; ++b) {
            if (c_[b].bypass) continue;
            for (int i = 0; i < n; ++i) {
                l[i] = s_[0][b].run(c_[b], l[i]);
                r[i] = s_[1][b].run(c_[b], r[i]);
            }
        }
    }
private:
    BiquadCoefs c_[3];
    BiquadState s_[2][3];
};

/**
 * Limiter "brickwall" com lookahead (63 amostras ≈ 1,3 ms), estéreo-linkado.
 * Técnica: ganho necessário r[n]; m = mínimo de r nas últimas N amostras; g = média móvel de m
 * (N amostras); a amostra atrasada (N-1) fica sempre coberta por todas as janelas -> pico <= teto.
 * Release exponencial (~120 ms) aplicado só quando o ganho sobe.
 */
class Limiter {
public:
    static constexpr int N = 64;
    void setCeiling(float c) { ceil_ = std::max(0.05f, std::min(1.0f, c)); }
    float ceiling() const { return ceil_; }

    void process(float* l, float* r, int n) {
        for (int i = 0; i < n; ++i) {
            dl_[w_] = l[i]; dr_[w_] = r[i];
            const float peak = std::max(std::fabs(l[i]), std::fabs(r[i]));
            rr_[w_] = peak > ceil_ ? ceil_ / peak : 1.0f;
            float m = 1.0f;
            for (int k = 0; k < N; ++k) m = std::min(m, rr_[k]);
            sum_ += m - mm_[w_]; mm_[w_] = m;
            const float g = sum_ * (1.0f / N);
            if (g < gr_) gr_ = g; else gr_ += (g - gr_) * kRelease;
            const int d = (w_ + 1) % N;  // amostra escrita há N-1 passos
            l[i] = dl_[d] * gr_; r[i] = dr_[d] * gr_;
            minGain_ = std::min(minGain_, gr_);
            w_ = (w_ + 1) % N;
        }
    }
    float takeMinGain() { const float g = minGain_; minGain_ = 1.0f; return g; }

private:
    static constexpr float kRelease = 1.0f / (0.12f * kEngineSampleRate);
    float ceil_ = 0.97f;
    float dl_[N] = {}, dr_[N] = {}, rr_[N], mm_[N];
    float sum_ = N, gr_ = 1.0f, minGain_ = 1.0f;
    int w_ = 0;
public:
    Limiter() { std::fill(rr_, rr_ + N, 1.0f); std::fill(mm_, mm_ + N, 1.0f); }
};

}  // namespace vs
