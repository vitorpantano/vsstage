#include "engine/TempoDetect.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace vs {

namespace {
constexpr int kHop = 128;
constexpr int kWin = 512;
}  // namespace

TempoResult detectTempo(const float* pcm, int64_t frames, int sr, double maxSeconds) {
    TempoResult res;
    const int64_t n = std::min<int64_t>(frames, static_cast<int64_t>(maxSeconds * sr));
    if (n < sr * 6) return res;

    // 1) Envelope de energia com pré-ênfase (realça ataques) por hop.
    const int64_t nh = (n - kWin) / kHop;
    std::vector<float> env(static_cast<size_t>(nh));
    float prevL = 0.f;
    for (int64_t h = 0; h < nh; ++h) {
        double e = 0;
        for (int i = 0; i < kWin; ++i) {
            const int64_t k = h * kHop + i;
            const float m = 0.5f * (pcm[2 * k] + pcm[2 * k + 1]);
            const float d = m - 0.97f * prevL;  // high-pass simples
            prevL = m;
            e += static_cast<double>(d) * d;
        }
        env[static_cast<size_t>(h)] = std::log1p(static_cast<float>(e / kWin) * 1.0e4f);
    }
    // 2) Fluxo de onset (aumento positivo) com subtração de média local.
    std::vector<float> flux(env.size(), 0.f);
    for (size_t i = 1; i < env.size(); ++i) flux[i] = std::max(0.f, env[i] - env[i - 1]);
    {
        const int w = 40;
        std::vector<float> f2(flux.size());
        for (size_t i = 0; i < flux.size(); ++i) {
            const size_t a = i > (size_t)w ? i - w : 0, b = std::min(flux.size(), i + w + 1);
            double s = 0;
            for (size_t j = a; j < b; ++j) s += flux[j];
            f2[i] = std::max(0.f, flux[i] - static_cast<float>(s / (b - a)) * 1.0f);
        }
        flux.swap(f2);
    }
    const double hopSec = static_cast<double>(kHop) / sr;
    const int N = static_cast<int>(flux.size());

    // 3) Autocorrelação nos atrasos de 60..180 BPM.
    auto lagOf = [&](double bpm) { return 60.0 / bpm / hopSec; };
    const int lmin = static_cast<int>(lagOf(180.0)), lmax = static_cast<int>(lagOf(60.0)) + 1;
    std::vector<double> ac(static_cast<size_t>(lmax + 2), 0.0);
    for (int l = lmin - 1; l <= lmax + 1; ++l) {
        double s = 0;
        for (int i = 0; i + l < N; ++i) s += static_cast<double>(flux[i]) * flux[i + l];
        ac[static_cast<size_t>(l)] = s / (N - l);
    }
    // Reforço harmônico: um andamento verdadeiro também correlaciona em 2x o atraso.
    double best = -1, bestLag = 0, mean = 0;
    int cnt = 0;
    for (int l = lmin; l <= lmax; ++l) {
        double v = ac[static_cast<size_t>(l)];
        if (2 * l < static_cast<int>(ac.size()) + 0) {}
        double v2 = 0;
        {   // atraso duplo (calcula direto se fora da tabela)
            const int l2 = 2 * l;
            double s = 0;
            for (int i = 0; i + l2 < N; ++i) s += static_cast<double>(flux[i]) * flux[i + l2];
            v2 = s / std::max(1, N - l2);
        }
        const double bpm = 60.0 / (l * hopSec);
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 100.0) / 0.9, 2.0));  // leve preferência por ~100 BPM
        const double score = (v + 0.5 * v2) * (0.6 + 0.4 * prior);
        mean += v; ++cnt;
        if (score > best && l > lmin && l < lmax && v >= ac[static_cast<size_t>(l - 1)] && v >= ac[static_cast<size_t>(l + 1)]) {
            best = score; bestLag = l;
        }
    }
    if (bestLag <= 0 || cnt == 0) return res;
    mean /= cnt;
    const double coarseBpm = 60.0 / (bestLag * hopSec);

    // 4) Refino: pente de batidas em passos de 0,05 BPM, com busca de fase.
    auto fluxAt = [&](double pos) {
        const int i = static_cast<int>(pos);
        if (i < 1 || i + 2 >= N) return 0.0;
        const double fr = pos - i;
        // máximo local em ±1 amostra, interpolado, tolera jitter de onset
        const double a = std::max({flux[i - 1], flux[i], flux[i + 1]});
        const double b = std::max({flux[i], flux[i + 1], flux[i + 2]});
        return a * (1 - fr) + b * fr;
    };
    double totalFlux = 0;
    for (float f : flux) totalFlux += f;
    if (totalFlux <= 0) return res;
    double wFlux = 0;
    for (int i = 0; i < std::min(N, static_cast<int>(30.0 / hopSec)); ++i) wFlux += flux[static_cast<size_t>(i)];
    if (wFlux <= 0) return res;
    struct Fit { double score = -1, bpm = 0, phase = 0; };
    auto refine = [&](double b0) {
        Fit f;
        const int Nw = std::min(N, static_cast<int>(30.0 / hopSec));  // decide em 30 s: a deriva do BPM ainda é pequena
        for (double bpm = b0 * 0.97; bpm <= b0 * 1.03; bpm += 0.02) {
            const double per = lagOf(bpm);
            const int phases = static_cast<int>(per);
            for (int ph = 0; ph < phases; ++ph) {
                double s = 0;
                for (double p = ph; p < Nw - 3; p += per) s += fluxAt(p);
                s /= wFlux;  // fração da energia de onsets explicada pela grade
                if (s > f.score) { f.score = s; f.bpm = bpm; f.phase = ph; }
            }
        }
        return f;
    };
    // Candidatos de oitava (x0.5, x1, x2) dentro de 60..180; começa no mais lento e só "sobe"
    // se a grade mais rápida explicar claramente mais onsets (evita 120 -> 60 e 75 -> 150).
    double cands[3] = {coarseBpm * 0.5, coarseBpm, coarseBpm * 2.0};
    Fit chosen;
    bool have = false;
    for (double c : cands) {
        if (c < 60.0 || c > 180.0) continue;
        Fit f = refine(c);
        if (getenv("TD_DEBUG")) fprintf(stderr, "cand %.2f -> %.2f score %.3f\n", c, f.bpm, f.score);
        if (!have) { chosen = f; have = true; }
        else if (f.score > chosen.score * 1.3) chosen = f;
    }
    if (!have) return res;
    double per = lagOf(chosen.bpm), phase = chosen.phase;
    const double bestScore = chosen.score;
    // Regressão linear posição-do-onset x índice-da-batida na música inteira: período e fase exatos.
    for (int pass = 0; pass < 3; ++pass) {
        double sk = 0, st = 0, skk = 0, skt = 0, sw = 0;
        const double win = per * 0.15;
        for (int64_t k = 0;; ++k) {
            const double pos = phase + k * per;
            if (pos >= N - 4) break;
            int bi = -1;
            float bv = 0;
            for (int i = static_cast<int>(pos - win); i <= static_cast<int>(pos + win); ++i)
                if (i > 0 && i < N && flux[static_cast<size_t>(i)] > bv) { bv = flux[static_cast<size_t>(i)]; bi = i; }
            if (bi < 0 || bv <= 0) continue;
            const double w = bv;
            sw += w; sk += w * k; st += w * bi; skk += w * k * k; skt += w * k * bi;
        }
        const double den = sw * skk - sk * sk;
        if (sw <= 0 || std::fabs(den) < 1e-9) break;
        per = (sw * skt - sk * st) / den;
        phase = (st - per * sk) / sw;
    }
    while (phase >= per) phase -= per;
    while (phase < 0) phase += per;
    const double bestBpm = 60.0 / (per * hopSec), bestPhase = phase;
    res.bpm = std::round(bestBpm * 100.0) / 100.0;
    res.offsetSec = bestPhase * hopSec + 0.008;  // compensa o atraso da janela do envelope (~8 ms, medido)
    res.confidence = bestScore;
    (void)mean;
    return res;
}

}  // namespace vs
