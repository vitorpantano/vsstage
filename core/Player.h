#pragma once
// Player: reúne o motor multipista, o dispositivo de áudio e o carregamento assíncrono das faixas.
// Tudo é chamado da thread principal (UI); update() deve rodar a cada quadro.
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Audio.h"
#include "core/Decoder.h"
#include "core/Library.h"
#include "core/Types.h"
#include "engine/MultitrackEngine.h"

namespace vss {

struct PlayerTrack {
    int slot = -1;
    std::string name, file;
    Role role = Role::Other;
    vs::Bus bus = vs::Bus::Band;
    float gain = 1.0f;
    bool mute = false, solo = false, pitchLock = false;
    float peak = 0.f;          // pico suavizado (0..1) para os medidores
    double detectedBpm = 0.0, detectedOffsetSec = 0.0, detectedConf = 0.0;
    bool loading = true;
    bool failed = false;
    std::string error;
};

class Player {
public:
    Player();
    ~Player();

    vs::MultitrackEngine& engine() { return engine_; }
    AudioDevice& audio() { return audio_; }

    // ---- Estado da música ----
    std::vector<PlayerTrack> tracks;
    SongMap map;
    std::string title;
    int64_t songId = -1;
    bool analyzedBpmPending = false;     // BPM/offset detectados nesta carga e ainda não gravados
    bool mapDirty = false;               // seções editadas
    int transposeSemis = 0;
    double targetBpm = 0.0;              // 0 = original
    int padKey = -1;
    float padGain = 0.8f;
    float masterGain = 1.0f;
    float limiterReductionDb = 0.f;
    std::string lastError;

    int pendingLoads() const { return pending_.load(); }
    bool isLoaded() const { return songId >= 0 && pending_.load() == 0; }

    // ---- Carregar música ----
    /// Limpa tudo e carrega as faixas da música (decodificação em threads). Callback "onLoaded" quando todas terminarem.
    void loadSong(const Library& lib, const SongEntry& song, const std::function<void()>& onLoaded = {});
    void clear();
    void update();                        // por quadro: instala faixas prontas, lê medidores

    // ---- Transporte ----
    void togglePlay();
    void stop();
    void play();
    void pause();
    bool playing() const { return engine_.isPlaying(); }
    int64_t position() const { return engine_.positionFrames(); }
    int64_t duration() const { return engine_.durationFrames(); }
    double positionSec() const { return static_cast<double>(position()) / vs::kEngineSampleRate; }
    void seekFrame(int64_t f);
    void jumpToSection(int index, vs::Quantize q = vs::Quantize::NextBar);
    void jumpRelativeSection(int delta);
    int currentSection() const;
    int pendingSection() const;           // salto agendado (índice) ou -1
    void setLoopSection(int index, bool enabled);
    bool loopEnabled() const { return engine_.loopEnabled(); }
    int loopSectionIndex() const { return loopSection_; }

    // ---- Faixas ----
    void setGain(size_t i, float g);
    void setMute(size_t i, bool m);
    void setSolo(size_t i, bool s);
    void setPitchLock(size_t i, bool l);
    void setBus(size_t i, vs::Bus b);
    void moveTrack(size_t from, size_t to);

    // ---- Tom / andamento ----
    void setTranspose(int semis);
    void setTargetBpm(double bpm);       // 0 = original
    void stepBpm(double delta);
    double currentBpm() const { return targetBpm > 0 ? targetBpm : map.bpm; }
    void setMasterGain(float g);
    void applyMaster(const Settings& s);

    // ---- Pad ----
    void padSelect(int key);             // -1 = desliga
    void setPadGain(float g);

    // ---- Mapa / seções ----
    void setSections(std::vector<Section> s);
    void markSectionHere();              // adiciona seção no compasso atual (ou move a mais próxima)
    void applyDetected(bool force);      // usa o BPM/offset detectado (click > demais)
    bool hasCustomSections = false;
    void applyMap();                     // empurra BPM/offset ao motor
    int totalBars() const;

    // ---- Saída ----
    bool startAudio(const Settings& s);
    void applyChannelMap(const std::vector<int>& m);
    void applyRoutingMode(int mode);

private:
    struct Loaded { uint64_t gen; int idx; std::unique_ptr<vs::TrackBuffer> buf; double bpm = 0, off = 0, conf = 0; std::string err; };
    void installLoaded(Loaded& l);
    void finishLoad();
    void recomputeTempo();
    void startWorkers(std::vector<std::pair<int, std::filesystem::path>> jobs, uint64_t gen);
    void joinWorkers();

    vs::MultitrackEngine engine_;
    AudioDevice audio_;
    std::mutex qm_;
    std::deque<Loaded> done_;
    std::vector<std::thread> workers_;
    std::atomic<int> pending_{0};
    std::atomic<uint64_t> gen_{0};
    std::atomic<bool> cancel_{false};
    std::function<void()> onLoaded_;
    bool finished_ = true;
    int loopSection_ = -1;
    std::vector<Section> autoSections_;
};

}  // namespace vss
