#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "engine/AtomicUtil.h"
#include "engine/Constants.h"
#include "engine/MasterBus.h"
#include "engine/PadPlayer.h"
#include "engine/SongGrid.h"
#include "engine/StretchVoice.h"
#include "engine/TrackBuffer.h"

namespace vs {

/**
 * Núcleo de mixagem multipista (Fase 2). Sem dependência de Oboe/NDK: render() é chamada pelo
 * callback de áudio e pelos testes no host.
 *
 * MODELO DE TEMPO
 *  - "Posição audível" a (frames de ORIGEM, tempo da gravação) é o que o músico ouve agora.
 *  - Faixas "secas" (sem processamento) leem x(a). Faixas "molhadas" passam por um StretchVoice
 *    alimentado em F = a + L(tempo), com L = inputLatency + outputLatency*tempo (medido).
 *  - Faixas com pitchLock NÃO sofrem pitch-shift; só entram no stretch quando o tempo != 1.
 *  - Mudanças dry<->wet usam "warm-up" em segundo plano + crossfade (sem estalo, sem silêncio).
 *  - Saltos (seek / Quantized Jump / Loop): um 2º banco de vozes é pré-aquecido no destino e
 *    trocado por crossfade de 5 ms exatamente na fronteira (batida/compasso).
 *  - Início do play com faixas processadas: pré-aquece por ~140 ms (latência de partida).
 */
class MultitrackEngine {
public:
    MultitrackEngine();
    ~MultitrackEngine();
    MultitrackEngine(const MultitrackEngine&) = delete;
    MultitrackEngine& operator=(const MultitrackEngine&) = delete;

    // ---------- Thread de controle ----------
    bool hasFreeSlot();
    int32_t installTrack(std::unique_ptr<TrackBuffer> buffer, Bus bus, float gain, bool soloSafe,
                         bool pitchLock = false);
    bool removeTrack(int32_t slot);
    void removeAllTracks();
    int64_t trackFrames(int32_t slot);

    void setTrackGain(int32_t slot, float gain);
    void setTrackMute(int32_t slot, bool mute);
    void setTrackSolo(int32_t slot, bool solo);
    void setTrackSoloSafe(int32_t slot, bool soloSafe);
    void setTrackPitchLock(int32_t slot, bool lock);
    void setTrackBus(int32_t slot, Bus bus);
    float consumeTrackPeak(int32_t slot);

    void setRoutingMode(RoutingMode mode) { routing_.store(static_cast<int32_t>(mode)); }
    void setMasterGain(float gain);

    // Tom (semitons -6..+6) e andamento (razão: 1.0 = BPM original; 1.1 = 10% mais rápido)
    void setTranspose(int32_t semitones);
    void setTempoRatio(double ratio);
    double tempoRatio() const { return tempoTarget_.load(std::memory_order_relaxed); }
    int32_t transpose() const { return semitones_.load(std::memory_order_relaxed); }

    // Grade musical (BPM original, compasso, deslocamento do 1º tempo em frames)
    void setGrid(double bpm, int beatsPerBar, double offsetFrames) { grid_.set(bpm, beatsPerBar, offsetFrames); }
    const SongGrid& grid() const { return grid_; }

    // Transporte
    void play();
    void pause();
    void stop();
    void seek(int64_t frame);                                  // salto imediato (alinhado)
    void jumpTo(int64_t frame, Quantize q);                    // Quantized Jump
    void setLoop(int64_t startFrame, int64_t endFrame, bool enabled);
    int64_t pendingJumpTarget() const { return pendingTargetPub_.load(std::memory_order_relaxed); }

    int64_t positionFrames() const { return position_.load(std::memory_order_relaxed); }
    int64_t durationFrames() const { return durationFrames_.load(std::memory_order_relaxed); }
    bool isPlaying() const { return playing_.load(std::memory_order_acquire); }
    bool isStarting() const { return starting_.load(std::memory_order_relaxed); }
    bool loopEnabled() const { return loopEnabled_.load(std::memory_order_relaxed); }

    // Pads
    bool installPad(int32_t key, std::unique_ptr<TrackBuffer> buffer);
    bool hasPad(int32_t key);
    bool generateSynthPad(int32_t key);
    void padSelect(int32_t key) { pads_.select(key); }
    void padSetGain(float g) { pads_.setGain(g); }
    void padSetFadeMs(float ms) { pads_.setFadeMs(ms); }
    int32_t padActiveKey() const { return pads_.activeKey(); }

    // Master bus
    void setEq(float lowDb, float midDb, float highDb);
    void setLimiter(bool enabled, float ceilingDb);
    float takeLimiterReductionDb();  // redução máx. desde a última leitura (dB, <= 0)

    void setStreamRunning(bool running) { streamRunning_.store(running, std::memory_order_release); }

    // ---------- Thread de áudio ----------
    /// `channels` = canais do stream (2 = estéreo, roteamento por RoutingMode; >2 ou mapa ativo = setChannelMap).
    void render(float* out, int32_t numFrames, int32_t channels = 2);

    /// Mapa canal de saída -> fonte (OutSrc). count = 0 desativa (volta ao RoutingMode). Seguro de qualquer thread.
    void setChannelMap(const int32_t* srcs, int32_t count);

private:
    struct TrackRt {
        StretchVoice v[2];
        int cur = 0;
        enum Mode { Dry, Warming, Wet } mode = Dry;
        int64_t warmLeft = 0;
        float wm = 0.f;          // mistura dry->wet (0..1)
        bool oldActive = false;  // voz antiga ainda sumindo após um salto
        bool incWarming = false; // voz v[1-cur] pré-aquecendo para um salto pendente
    };
    struct Slot {
        std::atomic<const TrackBuffer*> data{nullptr};
        TrackRt* rt = nullptr;
        std::atomic<float> gain{1.0f};
        std::atomic<bool> mute{false}, solo{false}, soloSafe{false}, pitchLock{false};
        std::atomic<int32_t> bus{0};
        std::atomic<float> peak{0.0f};
        float smoothed = 0.0f;
    };
    struct Pending {
        bool active = false, posBased = false, needWet = false, warming = false, isStart = false, isLoop = false;
        double target = 0, boundaryPos = 0, incF = 0;
        int64_t framesLeft = 0;
    };

    void renderImpl(float* out, int32_t numFrames);
    void applyControl();
    int32_t processChunk(float* out, int32_t maxN, bool wantPlay);
    void finishChunk(float* out, int32_t n);
    void mixTracks(int32_t n);
    void updateTempo(int32_t n);
    void evaluateModes();
    bool trackNeedsWet(const Slot& s) const;
    bool anyNeedsWet();
    bool allWetReady();
    void scheduleStart();
    void scheduleJump(double target, Quantize q);
    void startIncWarm();
    void executePending();
    void cancelPending();
    void resetTrackModes();
    void recomputeDurationLocked();
    void waitForRenderQuiescence();
    bool validSlot(int32_t s) const { return s >= 0 && s < kMaxTracks; }

    double lsrc(double tempo) const { return inLat_ + outLat_ * tempo; }
    double audPos() const { return F_ - lsrc(tempo_); }
    int64_t warmFrames() const;

    std::array<Slot, kMaxTracks> slots_;
    std::mutex controlMutex_;
    SongGrid grid_;
    PadPlayer pads_;

    // Transporte (UI -> áudio)
    std::atomic<bool> playing_{false};
    std::atomic<bool> stopPending_{false};
    std::atomic<int64_t> seekRequest_{-1};
    std::atomic<int64_t> jumpRequest_{-1};  // (quant << 56) | frame
    std::atomic<int64_t> position_{0};
    std::atomic<int64_t> durationFrames_{0};
    std::atomic<int64_t> pendingTargetPub_{-1};
    std::atomic<bool> starting_{false};
    std::atomic<int32_t> semitones_{0};
    std::atomic<double> tempoTarget_{1.0};
    std::atomic<uint32_t> modeVersion_{0};
    std::atomic<bool> loopEnabled_{false};
    std::atomic<int64_t> loopStart_{0}, loopEnd_{0};
    std::atomic<int32_t> routing_{static_cast<int32_t>(RoutingMode::SplitCueLeftBandRight)};
    std::atomic<float> masterGain_{1.0f};

    // Master bus (UI -> áudio)
    std::atomic<float> eqLow_{0}, eqMid_{0}, eqHigh_{0};
    std::atomic<uint32_t> eqVersion_{0};
    std::atomic<float> limCeiling_{0.97f};
    std::atomic<float> limMinGain_{1.0f};

    std::atomic<bool> streamRunning_{false};
    std::atomic<uint64_t> callbackCount_{0};

    // ----- Estado exclusivo do thread de áudio -----
    int inLat_ = 0, outLat_ = 0;
    bool hasCurrent_ = false;
    double aStart_ = 0;        // posição de partida quando !hasCurrent_
    double F_ = 0;             // posição de alimentação (banco atual)
    double tempo_ = 1.0;       // andamento aplicado
    Pending pend_;
    int64_t xfadeLeft_ = 0;    // crossfade pós-salto
    double Fold_ = 0;          // alimentação do banco antigo
    int64_t oldDryA0_ = 0;
    uint32_t lastModeVersion_ = 0;
    uint32_t lastEqVersion_ = 0xFFFFFFFF;
    float fadeGain_ = 0.f;
    float masterSmoothed_ = 1.f;
    Eq3 eq_;
    Limiter limBand_, limCue_, limGuide_;   // limCue_ = Click; limGuide_ = Guia
    std::atomic<int32_t> chanMap_[kMaxOutChannels];
    std::atomic<int32_t> chanMapCount_{0};
    int32_t outChannels_ = 2;
    std::vector<float> bandL_, bandR_, cueL_, cueR_, guideL_, guideR_, inL_, inR_, wNew_;
    std::vector<float> dNL_, dNR_, dOL_, dOR_, wNL_, wNR_, wOL_, wOR_;
};

}  // namespace vs
