#pragma once
#include <cstdint>
#include <vector>

namespace vs {

// Áudio de uma faixa já decodificado: float32 intercalado ESTÉREO (L,R,L,R...) em kEngineSampleRate.
// Imutável depois de publicado ao thread de áudio.
struct TrackBuffer {
    std::vector<float> pcm;
    int64_t frames = 0;
};

}  // namespace vs
