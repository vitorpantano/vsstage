#pragma once
#include <cstdint>

namespace vs {

struct TempoResult {
    double bpm = 0.0;        // 0 = não detectado
    double offsetSec = 0.0;  // posição do 1º tempo (fase da grade de batidas), em segundos
    double confidence = 0.0; // 0..~1+ (>0.25 costuma ser confiável)
};

/// Estima BPM e fase da batida de áudio estéreo intercalado @48k. Roda offline (fora do thread de áudio).
/// Analisa até maxSeconds do início. Faixa de busca: 60..180 BPM.
TempoResult detectTempo(const float* interleavedStereo, int64_t frames, int sampleRate, double maxSeconds = 150.0);

}  // namespace vs
