#pragma once
#include <algorithm>
#include <cstdint>

#include "engine/Constants.h"
#include "engine/TrackBuffer.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace vs {

/**
 * Uma "voz" de time-stretch/pitch-shift estéreo (Signalsmith Stretch, preset "cheaper" com
 * splitComputation = carga de CPU distribuída entre callbacks).
 *
 * Latência medida (host): o áudio de saída atrasa  L = inputLatency + outputLatency * tempo  frames
 * (de origem) em relação à posição de alimentação da entrada. O motor usa essa fórmula para
 * alinhar faixas processadas e não processadas.
 *
 * ALOCAÇÃO só no construtor (thread de controle). process()/start() não alocam.
 */
class StretchVoice {
public:
    StretchVoice() { s_.presetCheaper(2, kEngineSampleRate, true); }

    int inputLatency() { return s_.inputLatency(); }
    int outputLatency() { return s_.outputLatency(); }

    void start(float semitones) {
        s_.reset();
        s_.setTransposeSemitones(semitones);
        semis_ = semitones;
    }
    void setSemis(float semitones) {
        if (semitones != semis_) { s_.setTransposeSemitones(semitones); semis_ = semitones; }
    }

    // Alimenta `inCount` frames da fonte a partir de `inStart` (zeros fora do arquivo) e produz
    // `n` frames planares em outL/outR. inL/inR = scratch com >= inCount floats.
    void process(const TrackBuffer* src, int64_t inStart, int inCount, int n,
                 float* inL, float* inR, float* outL, float* outR) {
        for (int i = 0; i < inCount; ++i) {
            const int64_t k = inStart + i;
            if (k >= 0 && k < src->frames) { inL[i] = src->pcm[k * 2]; inR[i] = src->pcm[k * 2 + 1]; }
            else { inL[i] = 0.f; inR[i] = 0.f; }
        }
        float* in[2] = {inL, inR};
        float* out[2] = {outL, outR};
        s_.process(in, inCount, out, n);
    }

private:
    signalsmith::stretch::SignalsmithStretch<float> s_;
    float semis_ = 0.f;
};

}  // namespace vs
