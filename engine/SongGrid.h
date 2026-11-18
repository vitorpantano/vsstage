#pragma once
#include <atomic>
#include <cmath>

#include "engine/Constants.h"

namespace vs {

/**
 * Grade musical da música (BPM ORIGINAL do arquivo, compasso e deslocamento do 1º tempo),
 * em "frames de origem" (tempo da gravação, independente do time-stretch aplicado).
 * Escrita pelo thread de controle, lida pelo de áudio (atomics; leitura levemente inconsistente é inofensiva).
 */
class SongGrid {
public:
    void set(double bpm, int beatsPerBar, double offsetFrames) {
        bpm_.store(bpm > 1.0 ? bpm : 120.0, std::memory_order_relaxed);
        beatsPerBar_.store(beatsPerBar > 0 ? beatsPerBar : 4, std::memory_order_relaxed);
        offset_.store(offsetFrames, std::memory_order_relaxed);
    }
    double bpm() const { return bpm_.load(std::memory_order_relaxed); }
    int beatsPerBar() const { return beatsPerBar_.load(std::memory_order_relaxed); }
    double offsetFrames() const { return offset_.load(std::memory_order_relaxed); }

    double framesPerBeat() const { return 60.0 * kEngineSampleRate / bpm(); }
    double framesPerBar() const { return framesPerBeat() * beatsPerBar(); }

    /** Primeira linha de batida/compasso em posição >= pos (com tolerância de meio frame). */
    double boundaryAtOrAfter(double pos, Quantize q) const {
        if (q == Quantize::Immediate) return pos;
        const double step = (q == Quantize::NextBar) ? framesPerBar() : framesPerBeat();
        const double k = std::ceil((pos - offsetFrames() - 0.5) / step);
        return offsetFrames() + std::max(0.0, k) * step;
    }

    /** Frame do início do compasso `bar` (0 = primeiro compasso). */
    double barStartFrame(int bar) const { return offsetFrames() + bar * framesPerBar(); }

private:
    std::atomic<double> bpm_{120.0};
    std::atomic<int> beatsPerBar_{4};
    std::atomic<double> offset_{0.0};
};

}  // namespace vs
