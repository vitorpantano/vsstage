#pragma once
#include <cstdint>

namespace vs {

// Taxa interna FIXA. Todo áudio é convertido para ela no carregamento (offline).
// O Oboe/AAudio faz a conversão para a taxa real do dispositivo, então a mixagem
// nunca depende do hardware e as faixas permanecem alinhadas amostra a amostra.
constexpr int32_t kEngineSampleRate = 48000;

constexpr int32_t kMaxTracks = 32;
constexpr int32_t kRenderChunk = 512;                // frames por bloco interno de mixagem
constexpr int32_t kTransportFadeFrames = 720;        // 15 ms: fade ao dar play/pause (evita "clique")
constexpr float   kMasterSmoothCoef = 0.0015f;       // suavização do master gain (one-pole)
constexpr float   kMaxTrackGain = 2.0f;              // +6 dB de headroom no fader

// Barramentos lógicos. Click e Guide vão para o "cue bus"; Band para o "band bus".
enum class Bus : int32_t { Band = 0, Click = 1, Guide = 2 };

// Quantização de saltos (Quantized Jump)
enum class Quantize : int32_t { Immediate = 0, NextBeat = 1, NextBar = 2 };

constexpr int32_t kNumPadKeys = 12;                  // C..B
constexpr int32_t kXfadeFrames = 240;                // 5 ms: crossfade no instante do salto
constexpr int32_t kWetXfadeFrames = 960;             // 20 ms: transição entre faixa "seca" e processada
constexpr double  kTempoSlewPerSec = 0.5;            // velocidade máxima de mudança do tempo (razão/seg)
constexpr double  kMinTempo = 0.5;
constexpr double  kMaxTempo = 2.0;

/// Fontes que um canal de saída pode receber (matriz de roteamento multicanal / USB).
enum class OutSrc : int32_t {
    Off = 0, BandL = 1, BandR = 2, BandMono = 3, Click = 4, Guide = 5, CueMix = 6,  // CueMix = Click+Guide
    FullL = 7, FullR = 8                                                               // banda + cue (estéreo "tudo")
};
constexpr int32_t kMaxOutChannels = 16;

enum class RoutingMode : int32_t {
    Stereo = 0,               // Band em L/R, Click+Guide centralizados em ambos (modo de teste/fones)
    SplitCueLeftBandRight = 1 // Click+Guide (mono) 100% em L; Banda (mono) 100% em R  -> cabo P2 split
};

// Códigos de retorno (negativos = erro)
enum Err : int32_t {
    kOk = 0,
    kErrNoSlot = -1,
    kErrOpen = -2,
    kErrDecode = -3,
    kErrUnsupported = -4,
    kErrOutOfMemory = -5,
    kErrBadArg = -6
};

}  // namespace vs
