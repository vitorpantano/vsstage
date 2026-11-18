#include "engine/MultitrackEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace vs {

namespace {
constexpr int kScratchIn = 2 * kRenderChunk + 16;
constexpr int64_t kQuantShift = 56;
}  // namespace

MultitrackEngine::MultitrackEngine() {
    auto z = [](std::vector<float>& v, size_t n) { v.assign(n, 0.f); };
    z(bandL_, kRenderChunk); z(bandR_, kRenderChunk); z(cueL_, kRenderChunk); z(cueR_, kRenderChunk); z(guideL_, kRenderChunk); z(guideR_, kRenderChunk);
    z(inL_, kScratchIn); z(inR_, kScratchIn); z(wNew_, kRenderChunk);
    z(dNL_, kRenderChunk); z(dNR_, kRenderChunk); z(dOL_, kRenderChunk); z(dOR_, kRenderChunk);
    z(wNL_, kRenderChunk); z(wNR_, kRenderChunk); z(wOL_, kRenderChunk); z(wOR_, kRenderChunk);
    StretchVoice probe;
    inLat_ = probe.inputLatency();
    outLat_ = probe.outputLatency();
    eq_.setGains(0, 0, 0);
    limBand_.setCeiling(0.97f);
    limCue_.setCeiling(0.97f);
    limGuide_.setCeiling(0.97f);
    for (auto& c : chanMap_) c.store(0, std::memory_order_relaxed);
}

MultitrackEngine::~MultitrackEngine() {
    for (auto& s : slots_) {
        delete s.data.exchange(nullptr);
        delete s.rt;
    }
    for (auto& b : pads_.buffers) delete b.exchange(nullptr);
}

// =====================================================================================
// Thread de controle
// =====================================================================================
bool MultitrackEngine::hasFreeSlot() {
    std::lock_guard<std::mutex> lock(controlMutex_);
    for (auto& s : slots_) if (!s.data.load(std::memory_order_relaxed)) return true;
    return false;
}

int32_t MultitrackEngine::installTrack(std::unique_ptr<TrackBuffer> buffer, Bus bus, float gain,
                                       bool soloSafe, bool pitchLock) {
    if (!buffer || buffer->frames <= 0) return kErrBadArg;
    std::unique_ptr<TrackRt> rt;
    try { rt = std::make_unique<TrackRt>(); } catch (const std::bad_alloc&) { return kErrOutOfMemory; }
    std::lock_guard<std::mutex> lock(controlMutex_);
    for (int32_t i = 0; i < kMaxTracks; ++i) {
        Slot& s = slots_[i];
        if (s.data.load(std::memory_order_relaxed)) continue;
        s.gain.store(std::clamp(gain, 0.f, kMaxTrackGain), std::memory_order_relaxed);
        s.mute.store(false, std::memory_order_relaxed);
        s.solo.store(false, std::memory_order_relaxed);
        s.soloSafe.store(soloSafe, std::memory_order_relaxed);
        s.pitchLock.store(pitchLock, std::memory_order_relaxed);
        s.bus.store(static_cast<int32_t>(bus), std::memory_order_relaxed);
        s.peak.store(0.f, std::memory_order_relaxed);
        s.smoothed = 0.f;
        delete s.rt;
        s.rt = rt.release();
        s.data.store(buffer.release(), std::memory_order_release);
        recomputeDurationLocked();
        modeVersion_.fetch_add(1, std::memory_order_release);
        return i;
    }
    return kErrNoSlot;
}

void MultitrackEngine::waitForRenderQuiescence() {
    if (!streamRunning_.load(std::memory_order_acquire)) return;
    const uint64_t start = callbackCount_.load(std::memory_order_acquire);
    for (int i = 0; i < 250; ++i) {
        if (callbackCount_.load(std::memory_order_acquire) != start) return;
        if (!streamRunning_.load(std::memory_order_acquire)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool MultitrackEngine::removeTrack(int32_t slot) {
    if (!validSlot(slot)) return false;
    std::lock_guard<std::mutex> lock(controlMutex_);
    const TrackBuffer* old = slots_[slot].data.exchange(nullptr, std::memory_order_acq_rel);
    if (!old) return false;
    waitForRenderQuiescence();
    slots_[slot].smoothed = 0.f;
    delete old;
    delete slots_[slot].rt;
    slots_[slot].rt = nullptr;
    recomputeDurationLocked();
    modeVersion_.fetch_add(1, std::memory_order_release);
    return true;
}

void MultitrackEngine::removeAllTracks() { for (int32_t i = 0; i < kMaxTracks; ++i) removeTrack(i); }

int64_t MultitrackEngine::trackFrames(int32_t slot) {
    if (!validSlot(slot)) return 0;
    std::lock_guard<std::mutex> lock(controlMutex_);
    const TrackBuffer* d = slots_[slot].data.load(std::memory_order_acquire);
    return d ? d->frames : 0;
}

void MultitrackEngine::recomputeDurationLocked() {
    int64_t m = 0;
    for (auto& s : slots_) {
        const TrackBuffer* d = s.data.load(std::memory_order_acquire);
        if (d) m = std::max(m, d->frames);
    }
    durationFrames_.store(m, std::memory_order_release);
}

void MultitrackEngine::setTrackGain(int32_t s, float g) {
    if (validSlot(s)) slots_[s].gain.store(std::clamp(g, 0.f, kMaxTrackGain), std::memory_order_relaxed);
}
void MultitrackEngine::setTrackMute(int32_t s, bool v) { if (validSlot(s)) slots_[s].mute.store(v, std::memory_order_relaxed); }
void MultitrackEngine::setTrackSolo(int32_t s, bool v) { if (validSlot(s)) slots_[s].solo.store(v, std::memory_order_relaxed); }
void MultitrackEngine::setTrackSoloSafe(int32_t s, bool v) { if (validSlot(s)) slots_[s].soloSafe.store(v, std::memory_order_relaxed); }
void MultitrackEngine::setTrackPitchLock(int32_t s, bool v) {
    if (!validSlot(s)) return;
    slots_[s].pitchLock.store(v, std::memory_order_relaxed);
    modeVersion_.fetch_add(1, std::memory_order_release);
}
void MultitrackEngine::setTrackBus(int32_t s, Bus b) { if (validSlot(s)) slots_[s].bus.store(static_cast<int32_t>(b), std::memory_order_relaxed); }
float MultitrackEngine::consumeTrackPeak(int32_t s) { return validSlot(s) ? slots_[s].peak.exchange(0.f, std::memory_order_relaxed) : 0.f; }
void MultitrackEngine::setMasterGain(float g) { masterGain_.store(std::clamp(g, 0.f, 2.f), std::memory_order_relaxed); }

void MultitrackEngine::setTranspose(int32_t st) {
    semitones_.store(std::clamp(st, -6, 6), std::memory_order_relaxed);
    modeVersion_.fetch_add(1, std::memory_order_release);
}
void MultitrackEngine::setTempoRatio(double r) {
    tempoTarget_.store(std::clamp(r, kMinTempo, kMaxTempo), std::memory_order_relaxed);
    modeVersion_.fetch_add(1, std::memory_order_release);
}

void MultitrackEngine::play() {
    if (durationFrames_.load(std::memory_order_acquire) <= 0) return;
    stopPending_.store(false, std::memory_order_relaxed);
    if (position_.load(std::memory_order_relaxed) >= durationFrames_.load(std::memory_order_relaxed))
        seekRequest_.store(0, std::memory_order_release);
    playing_.store(true, std::memory_order_release);
}
void MultitrackEngine::pause() { playing_.store(false, std::memory_order_release); }
void MultitrackEngine::stop() {
    playing_.store(false, std::memory_order_release);
    stopPending_.store(true, std::memory_order_release);
    if (!streamRunning_.load(std::memory_order_acquire)) renderImpl(nullptr, 0);  // aplica o stop sem stream
}
void MultitrackEngine::seek(int64_t frame) {
    seekRequest_.store(std::max<int64_t>(0, frame), std::memory_order_release);
    if (!streamRunning_.load(std::memory_order_acquire)) renderImpl(nullptr, 0);
}
void MultitrackEngine::jumpTo(int64_t frame, Quantize q) {
    const int64_t packed = (static_cast<int64_t>(q) << kQuantShift) | std::max<int64_t>(0, frame);
    jumpRequest_.store(packed, std::memory_order_release);
    if (!streamRunning_.load(std::memory_order_acquire)) renderImpl(nullptr, 0);
}
void MultitrackEngine::setLoop(int64_t s, int64_t e, bool en) {
    loopStart_.store(s, std::memory_order_relaxed);
    loopEnd_.store(e, std::memory_order_relaxed);
    loopEnabled_.store(en && e > s, std::memory_order_release);
}

// ---------- Pads ----------
bool MultitrackEngine::installPad(int32_t key, std::unique_ptr<TrackBuffer> buffer) {
    if (key < 0 || key >= kNumPadKeys || !buffer || buffer->frames <= 0) return false;
    std::lock_guard<std::mutex> lock(controlMutex_);
    const TrackBuffer* old = pads_.buffers[key].exchange(buffer.release(), std::memory_order_acq_rel);
    if (old) { waitForRenderQuiescence(); delete old; }
    return true;
}
bool MultitrackEngine::hasPad(int32_t key) {
    return key >= 0 && key < kNumPadKeys && pads_.buffers[key].load(std::memory_order_acquire) != nullptr;
}
bool MultitrackEngine::generateSynthPad(int32_t key) {
    if (hasPad(key)) return true;
    return installPad(key, makeSynthPad(key));
}

// ---------- Master ----------
void MultitrackEngine::setEq(float l, float m, float h) {
    eqLow_.store(std::clamp(l, -12.f, 12.f), std::memory_order_relaxed);
    eqMid_.store(std::clamp(m, -12.f, 12.f), std::memory_order_relaxed);
    eqHigh_.store(std::clamp(h, -12.f, 12.f), std::memory_order_relaxed);
    eqVersion_.fetch_add(1, std::memory_order_release);
}
void MultitrackEngine::setLimiter(bool enabled, float ceilingDb) {
    limCeiling_.store(enabled ? std::pow(10.f, std::clamp(ceilingDb, -12.f, 0.f) / 20.f) : 1.0f, std::memory_order_relaxed);
}
float MultitrackEngine::takeLimiterReductionDb() {
    const float g = limMinGain_.exchange(1.0f, std::memory_order_relaxed);
    return 20.f * std::log10(std::max(g, 1e-4f));
}

// =====================================================================================
// Thread de áudio
// =====================================================================================
void MultitrackEngine::setChannelMap(const int32_t* srcs, int32_t count) {
    count = std::clamp(count, 0, kMaxOutChannels);
    for (int32_t i = 0; i < count; ++i) chanMap_[i].store(srcs[i], std::memory_order_relaxed);
    chanMapCount_.store(count, std::memory_order_release);
}

void MultitrackEngine::render(float* out, int32_t numFrames, int32_t channels) {
    outChannels_ = std::clamp(channels, 1, kMaxOutChannels);
    renderImpl(out, numFrames);
    callbackCount_.fetch_add(1, std::memory_order_release);
}

int64_t MultitrackEngine::warmFrames() const {
    return static_cast<int64_t>(std::ceil(lsrc(tempo_) / tempo_));
}

bool MultitrackEngine::trackNeedsWet(const Slot& s) const {
    const bool anyTempo = tempoTarget_.load(std::memory_order_relaxed) != 1.0 || tempo_ != 1.0;
    if (anyTempo) return true;
    return semitones_.load(std::memory_order_relaxed) != 0 && !s.pitchLock.load(std::memory_order_relaxed);
}
bool MultitrackEngine::anyNeedsWet() {
    for (auto& s : slots_) if (s.data.load(std::memory_order_acquire) && trackNeedsWet(s)) return true;
    return false;
}
bool MultitrackEngine::allWetReady() {
    for (auto& s : slots_) {
        if (!s.data.load(std::memory_order_acquire)) continue;
        if (s.rt->mode != TrackRt::Wet || s.rt->wm < 0.999f) return false;
    }
    return true;
}

void MultitrackEngine::resetTrackModes() {
    for (auto& s : slots_) {
        if (!s.data.load(std::memory_order_acquire)) continue;
        s.rt->mode = TrackRt::Dry; s.rt->wm = 0.f; s.rt->oldActive = false;
        s.rt->incWarming = false; s.rt->warmLeft = 0;
    }
}

void MultitrackEngine::cancelPending() {
    if (pend_.active) {
        for (auto& s : slots_) if (s.data.load(std::memory_order_acquire)) s.rt->incWarming = false;
    }
    pend_ = Pending{};
    pendingTargetPub_.store(-1, std::memory_order_relaxed);
    starting_.store(false, std::memory_order_relaxed);
}

void MultitrackEngine::applyControl() {
    const bool running = playing_.load(std::memory_order_acquire) || fadeGain_ > 0.f;
    const int64_t total = durationFrames_.load(std::memory_order_acquire);

    // Mudança de tom/tempo/pitchLock/faixas com o transporte parado: recomeça limpo (re-aquece no play)
    const uint32_t mv = modeVersion_.load(std::memory_order_acquire);
    if (mv != lastModeVersion_) {
        lastModeVersion_ = mv;
        if (!running && hasCurrent_) {
            aStart_ = std::max(0.0, audPos());
            hasCurrent_ = false;
            cancelPending();
            resetTrackModes();
        }
    }

    const int64_t sk = seekRequest_.exchange(-1, std::memory_order_acq_rel);
    if (sk >= 0) {
        const int64_t t = std::min(sk, total);
        if (hasCurrent_ && running && playing_.load(std::memory_order_relaxed)) {
            jumpRequest_.store(t, std::memory_order_relaxed);  // Quantize::Immediate = 0
        } else {
            aStart_ = static_cast<double>(t);
            hasCurrent_ = false;
            cancelPending();
            resetTrackModes();
            position_.store(t, std::memory_order_relaxed);
        }
    }
    const int64_t jr = jumpRequest_.load(std::memory_order_acquire);
    if (jr >= 0 && (!hasCurrent_ || !running)) {
        jumpRequest_.store(-1, std::memory_order_relaxed);
        aStart_ = static_cast<double>(std::min(jr & ((int64_t(1) << kQuantShift) - 1), total));
        hasCurrent_ = false;
        cancelPending();
        resetTrackModes();
        position_.store(static_cast<int64_t>(aStart_), std::memory_order_relaxed);
    }

    if (lastEqVersion_ != eqVersion_.load(std::memory_order_acquire)) {
        lastEqVersion_ = eqVersion_.load(std::memory_order_acquire);
        eq_.setGains(eqLow_.load(), eqMid_.load(), eqHigh_.load());
    }
    const float ceil = limCeiling_.load(std::memory_order_relaxed);
    if (ceil != limBand_.ceiling()) { limBand_.setCeiling(ceil); limCue_.setCeiling(ceil); limGuide_.setCeiling(ceil); }
}

void MultitrackEngine::renderImpl(float* out, int32_t numFrames) {
    if (!out) {  // chamada de controle sem stream: só aplica pedidos pendentes
        applyControl();
        const bool running = playing_.load(std::memory_order_acquire) || fadeGain_ > 0.f;
        if (!running && stopPending_.exchange(false, std::memory_order_acq_rel)) {
            hasCurrent_ = false; aStart_ = 0; cancelPending(); resetTrackModes();
            position_.store(0, std::memory_order_relaxed);
        }
        return;
    }
    applyControl();
    const bool wantPlay = playing_.load(std::memory_order_acquire);
    int32_t done = 0;
    while (done < numFrames) {
        done += processChunk(out + static_cast<size_t>(done) * static_cast<size_t>(outChannels_), std::min(kRenderChunk, numFrames - done), wantPlay);
    }
    int64_t pos = hasCurrent_ ? static_cast<int64_t>(std::max(0.0, audPos())) : static_cast<int64_t>(aStart_);
    position_.store(std::min(pos, durationFrames_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    limMinGain_.store(std::min(limMinGain_.load(std::memory_order_relaxed),
                               std::min(limBand_.takeMinGain(), std::min(limCue_.takeMinGain(), limGuide_.takeMinGain()))), std::memory_order_relaxed);
}

void MultitrackEngine::scheduleStart() {
    const int64_t total = durationFrames_.load(std::memory_order_acquire);
    if (aStart_ >= total) aStart_ = 0;
    tempo_ = tempoTarget_.load(std::memory_order_relaxed);
    pend_ = Pending{};
    pend_.active = true; pend_.isStart = true; pend_.posBased = false;
    pend_.target = aStart_;
    pend_.needWet = anyNeedsWet();
    pend_.framesLeft = pend_.needWet ? warmFrames() : 0;
    starting_.store(true, std::memory_order_relaxed);
}

void MultitrackEngine::scheduleJump(double target, Quantize q) {
    const int64_t total = durationFrames_.load(std::memory_order_acquire);
    target = std::clamp(target, 0.0, static_cast<double>(std::max<int64_t>(0, total - 1)));
    const bool need = anyNeedsWet();
    const double W = need ? static_cast<double>(warmFrames() + 64) : 0.0;
    const double minPos = audPos() + W * tempo_;
    pend_ = Pending{};
    pend_.active = true; pend_.posBased = true; pend_.needWet = need;
    pend_.target = target;
    pend_.boundaryPos = grid_.boundaryAtOrAfter(minPos, q);
    pendingTargetPub_.store(static_cast<int64_t>(target), std::memory_order_relaxed);
}

void MultitrackEngine::startIncWarm() {
    pend_.warming = true;
    pend_.incF = pend_.target;
    const int semis = semitones_.load(std::memory_order_relaxed);
    for (auto& s : slots_) {
        if (!s.data.load(std::memory_order_acquire)) continue;
        TrackRt* rt = s.rt;
        if (!trackNeedsWet(s)) continue;
        if (rt->mode == TrackRt::Warming) { rt->mode = TrackRt::Dry; rt->wm = 0.f; }
        rt->oldActive = false;  // a voz antiga será reaproveitada
        rt->incWarming = true;
        rt->v[1 - rt->cur].start(s.pitchLock.load(std::memory_order_relaxed) ? 0.f : static_cast<float>(semis));
    }
}

void MultitrackEngine::executePending() {
    const bool wasStart = !hasCurrent_;
    const double a0 = hasCurrent_ ? audPos() : 0.0;
    for (auto& s : slots_) {
        if (!s.data.load(std::memory_order_acquire)) continue;
        TrackRt* rt = s.rt;
        if (rt->incWarming) {
            const bool wasWet = rt->mode == TrackRt::Wet && rt->wm > 0.999f && !wasStart;
            rt->cur = 1 - rt->cur;
            rt->oldActive = wasWet;
            rt->mode = TrackRt::Wet;
            if (wasStart) rt->wm = 1.f;
            rt->incWarming = false;
        } else {
            if (rt->mode != TrackRt::Dry) { rt->mode = TrackRt::Dry; }
            rt->wm = 0.f;
            rt->oldActive = false;
        }
        rt->warmLeft = 0;
    }
    if (!wasStart) {
        xfadeLeft_ = kXfadeFrames;
        Fold_ = F_;
        oldDryA0_ = static_cast<int64_t>(std::llround(F_ - lsrc(1.0)));
        (void)a0;
    } else {
        xfadeLeft_ = 0;
    }
    F_ = pend_.warming ? pend_.incF : pend_.target + lsrc(tempo_);
    hasCurrent_ = true;
    cancelPending();
}

void MultitrackEngine::updateTempo(int32_t n) {
    const double target = tempoTarget_.load(std::memory_order_relaxed);
    if (!hasCurrent_) { tempo_ = target; return; }
    if (tempo_ == target) return;
    if (tempo_ == 1.0 && !allWetReady()) return;  // espera todas as faixas estarem processadas
    const double step = kTempoSlewPerSec * n / kEngineSampleRate;
    if (std::fabs(target - tempo_) <= step) tempo_ = target; else tempo_ += (target > tempo_ ? step : -step);
    if (std::fabs(tempo_ - 1.0) < 1e-6) { tempo_ = 1.0; F_ = std::round(F_); }
}

void MultitrackEngine::evaluateModes() {
    const int semis = semitones_.load(std::memory_order_relaxed);
    for (auto& s : slots_) {
        if (!s.data.load(std::memory_order_acquire)) continue;
        TrackRt* rt = s.rt;
        const bool need = trackNeedsWet(s);
        const float semisT = s.pitchLock.load(std::memory_order_relaxed) ? 0.f : static_cast<float>(semis);
        switch (rt->mode) {
            case TrackRt::Dry:
                if (need && !rt->incWarming) {
                    rt->mode = TrackRt::Warming;
                    rt->warmLeft = warmFrames();
                    rt->v[rt->cur].start(semisT);
                }
                break;
            case TrackRt::Warming:
                if (!need) rt->mode = TrackRt::Dry;
                break;
            case TrackRt::Wet:
                if (!need) rt->mode = TrackRt::Dry;  // wm desce a 0 (crossfade)
                break;
        }
        if (rt->mode != TrackRt::Dry || rt->wm > 0.f) rt->v[rt->cur].setSemis(semisT);
    }
}

namespace {
inline void readDry(const TrackBuffer* d, int64_t a0, int n, float* L, float* R) {
    for (int i = 0; i < n; ++i) {
        const int64_t k = a0 + i;
        if (k >= 0 && k < d->frames) { L[i] = d->pcm[k * 2]; R[i] = d->pcm[k * 2 + 1]; }
        else { L[i] = 0.f; R[i] = 0.f; }
    }
}
}  // namespace

int32_t MultitrackEngine::processChunk(float* out, int32_t maxN, bool /*wantPlayAtStart*/) {
    const bool wantPlay = playing_.load(std::memory_order_acquire);
    const bool running = wantPlay || fadeGain_ > 0.f;
    int32_t n = maxN;

    if (!running) {
        if (stopPending_.exchange(false, std::memory_order_acq_rel)) {
            hasCurrent_ = false; aStart_ = 0; cancelPending(); resetTrackModes();
            position_.store(0, std::memory_order_relaxed);
        }
        std::fill(bandL_.begin(), bandL_.begin() + n, 0.f); std::fill(bandR_.begin(), bandR_.begin() + n, 0.f);
        std::fill(cueL_.begin(), cueL_.begin() + n, 0.f); std::fill(cueR_.begin(), cueR_.begin() + n, 0.f);
        std::fill(guideL_.begin(), guideL_.begin() + n, 0.f); std::fill(guideR_.begin(), guideR_.begin() + n, 0.f);
        finishChunk(out, n);
        return n;
    }

    // ---- agenda partida / saltos / loop ----
    if (!hasCurrent_) {
        if (!pend_.active && wantPlay) scheduleStart();
    } else {
        const int64_t jr = jumpRequest_.exchange(-1, std::memory_order_acq_rel);
        if (jr >= 0) {
            cancelPending();
            scheduleJump(static_cast<double>(jr & ((int64_t(1) << kQuantShift) - 1)),
                         static_cast<Quantize>(jr >> kQuantShift));
        }
        const bool loopOn = loopEnabled_.load(std::memory_order_acquire);
        if (pend_.active && pend_.isLoop && !loopOn) cancelPending();
        if (!pend_.active && loopOn) {
            const double ls = static_cast<double>(loopStart_.load(std::memory_order_relaxed));
            const double le = static_cast<double>(loopEnd_.load(std::memory_order_relaxed));
            const double a = audPos();
            if (a >= ls && a < le) {
                const bool need = anyNeedsWet();
                const double W = need ? static_cast<double>(warmFrames() + 64) : 0.0;
                pend_ = Pending{};
                pend_.active = true; pend_.posBased = true; pend_.isLoop = true; pend_.needWet = need;
                pend_.target = ls;
                pend_.boundaryPos = std::max(le, a + W * tempo_);
                pendingTargetPub_.store(static_cast<int64_t>(ls), std::memory_order_relaxed);
            }
        }
    }

    if (hasCurrent_) { updateTempo(n); evaluateModes(); }

    // ---- contagem regressiva do salto pendente (e divisão do bloco nos eventos) ----
    if (pend_.active) {
        if (pend_.posBased && hasCurrent_)
            pend_.framesLeft = std::max<int64_t>(0, static_cast<int64_t>(std::ceil((pend_.boundaryPos - audPos()) / tempo_)));
        if (pend_.framesLeft <= 0) {
            executePending();
        } else {
            const int64_t W = pend_.needWet ? warmFrames() : 0;
            if (!pend_.warming && pend_.needWet && pend_.framesLeft <= W) startIncWarm();
            if (pend_.warming || !pend_.needWet) n = static_cast<int32_t>(std::min<int64_t>(n, pend_.framesLeft));
            else n = static_cast<int32_t>(std::min<int64_t>(n, pend_.framesLeft - W));
            n = std::max(1, n);
        }
    }

    mixTracks(n);

    // envelope de transporte (play/pause/stop sem clique) — só nas faixas, não nos pads
    {
        const float fadeStep = 1.0f / static_cast<float>(kTransportFadeFrames);
        const float tgt = (wantPlay && hasCurrent_) ? 1.0f : 0.0f;
        for (int32_t i = 0; i < n; ++i) {
            if (fadeGain_ < tgt) fadeGain_ = std::min(tgt, fadeGain_ + fadeStep);
            else if (fadeGain_ > tgt) fadeGain_ = std::max(tgt, fadeGain_ - fadeStep);
            bandL_[i] *= fadeGain_; bandR_[i] *= fadeGain_; cueL_[i] *= fadeGain_; cueR_[i] *= fadeGain_; guideL_[i] *= fadeGain_; guideR_[i] *= fadeGain_;
        }
    }
    finishChunk(out, n);

    // ---- avanço do tempo ----
    if (hasCurrent_) {
        F_ += n * tempo_;
        if (xfadeLeft_ > 0) {
            Fold_ += n * tempo_; oldDryA0_ += n; xfadeLeft_ -= n;
            if (xfadeLeft_ <= 0) {
                xfadeLeft_ = 0;
                for (auto& s : slots_) if (s.data.load(std::memory_order_acquire)) s.rt->oldActive = false;
            }
        }
    }
    if (pend_.active) {
        if (pend_.warming) pend_.incF += n * tempo_;
        if (!pend_.posBased) pend_.framesLeft -= n;
    }
    const int64_t total = durationFrames_.load(std::memory_order_relaxed);
    if (hasCurrent_ && audPos() >= static_cast<double>(total)) {  // fim da música
        playing_.store(false, std::memory_order_release);
        hasCurrent_ = false; aStart_ = static_cast<double>(total);
        cancelPending(); resetTrackModes(); fadeGain_ = 0.f;
    }
    return n;
}

void MultitrackEngine::mixTracks(int32_t n) {
    std::fill(bandL_.begin(), bandL_.begin() + n, 0.f); std::fill(bandR_.begin(), bandR_.begin() + n, 0.f);
    std::fill(cueL_.begin(), cueL_.begin() + n, 0.f); std::fill(cueR_.begin(), cueR_.begin() + n, 0.f);
        std::fill(guideL_.begin(), guideL_.begin() + n, 0.f); std::fill(guideR_.begin(), guideR_.begin() + n, 0.f);

    const bool cur = hasCurrent_;
    const bool xf = cur && xfadeLeft_ > 0;
    int64_t inStart = 0, oldInStart = 0, incInStart = 0, dryA0 = 0;
    int inCount = 0, oldInCount = 0, incInCount = 0;
    if (cur) {
        inStart = static_cast<int64_t>(std::floor(F_));
        inCount = static_cast<int>(static_cast<int64_t>(std::floor(F_ + n * tempo_)) - inStart);
        dryA0 = static_cast<int64_t>(std::llround(F_ - lsrc(1.0)));
    }
    if (xf) {
        oldInStart = static_cast<int64_t>(std::floor(Fold_));
        oldInCount = static_cast<int>(static_cast<int64_t>(std::floor(Fold_ + n * tempo_)) - oldInStart);
    }
    if (pend_.warming) {
        incInStart = static_cast<int64_t>(std::floor(pend_.incF));
        incInCount = static_cast<int>(static_cast<int64_t>(std::floor(pend_.incF + n * tempo_)) - incInStart);
    }
    for (int i = 0; i < n; ++i)
        wNew_[i] = xf ? std::clamp((kXfadeFrames - xfadeLeft_ + i + 1) / static_cast<float>(kXfadeFrames), 0.f, 1.f) : 1.f;

    bool anySolo = false;
    for (auto& s : slots_)
        if (s.data.load(std::memory_order_acquire) && s.solo.load(std::memory_order_relaxed)) { anySolo = true; break; }

    float* const inL = inL_.data(); float* const inR = inR_.data();
    const float wmStep = 1.0f / static_cast<float>(kWetXfadeFrames);

    for (auto& s : slots_) {
        const TrackBuffer* d = s.data.load(std::memory_order_acquire);
        if (!d) continue;
        TrackRt* rt = s.rt;

        if (rt->incWarming && incInCount >= 0)  // pré-aquece a voz do próximo salto (saída descartada)
            rt->v[1 - rt->cur].process(d, incInStart, incInCount, n, inL, inR, wOL_.data(), wOR_.data());
        if (!cur) continue;

        const bool curVoice = rt->mode != TrackRt::Dry || rt->wm > 0.f;
        if (curVoice) {
            rt->v[rt->cur].process(d, inStart, inCount, n, inL, inR, wNL_.data(), wNR_.data());
            if (rt->mode == TrackRt::Warming) {
                rt->warmLeft -= n;
                if (rt->warmLeft <= 0) rt->mode = TrackRt::Wet;
            }
        }
        const bool useOldWet = xf && rt->oldActive;
        if (useOldWet) rt->v[1 - rt->cur].process(d, oldInStart, oldInCount, n, inL, inR, wOL_.data(), wOR_.data());

        const bool audible = !s.mute.load(std::memory_order_relaxed) &&
                             (!anySolo || s.solo.load(std::memory_order_relaxed) || s.soloSafe.load(std::memory_order_relaxed));
        const float target = audible ? s.gain.load(std::memory_order_relaxed) : 0.f;
        const float g0 = s.smoothed;
        if (g0 == 0.f && target == 0.f) { s.smoothed = 0.f; continue; }

        const bool needDry = rt->wm < 1.f || rt->mode != TrackRt::Wet;
        if (needDry) {
            readDry(d, dryA0, n, dNL_.data(), dNR_.data());
            if (xf) readDry(d, oldDryA0_, n, dOL_.data(), dOR_.data());
        }
        const int32_t busId = s.bus.load(std::memory_order_relaxed);
        float* dstL = busId == static_cast<int32_t>(Bus::Band) ? bandL_.data()
                    : busId == static_cast<int32_t>(Bus::Guide) ? guideL_.data() : cueL_.data();
        float* dstR = busId == static_cast<int32_t>(Bus::Band) ? bandR_.data()
                    : busId == static_cast<int32_t>(Bus::Guide) ? guideR_.data() : cueR_.data();
        const float wmTarget = rt->mode == TrackRt::Wet ? 1.f : 0.f;
        const float gStep = (target - g0) / static_cast<float>(n);
        float g = g0, wm = rt->wm, pk = 0.f;
        for (int i = 0; i < n; ++i) {
            if (wm < wmTarget) wm = std::min(wmTarget, wm + wmStep); else if (wm > wmTarget) wm = std::max(wmTarget, wm - wmStep);
            const float wn = wNew_[i], wo = 1.f - wn;
            float l = 0.f, r = 0.f;
            if (wm < 1.f) {
                float dl = dNL_[i] * wn, dr = dNR_[i] * wn;
                if (xf) { dl += dOL_[i] * wo; dr += dOR_[i] * wo; }
                l += dl * (1.f - wm); r += dr * (1.f - wm);
            }
            if (wm > 0.f) {
                float wl = wNL_[i] * wn, wr = wNR_[i] * wn;
                if (useOldWet) { wl += wOL_[i] * wo; wr += wOR_[i] * wo; }
                l += wl * wm; r += wr * wm;
            }
            g += gStep;
            l *= g; r *= g;
            dstL[i] += l; dstR[i] += r;
            pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
        }
        rt->wm = wm;
        s.smoothed = target;
        atomicMax(s.peak, pk);
    }
}

void MultitrackEngine::finishChunk(float* out, int32_t n) {
    float* bl = bandL_.data(); float* br = bandR_.data();
    float* cl = cueL_.data(); float* cr = cueR_.data();
    float* gl = guideL_.data(); float* gr = guideR_.data();
    pads_.render(bl, br, n);          // pads: independentes do transporte
    eq_.process(bl, br, n);           // EQ 3 bandas só no barramento da banda (P.A.)
    limBand_.process(bl, br, n);      // limiter da banda
    limCue_.process(cl, cr, n);       // limiter do Click (mesma latência: mantém Click alinhado à banda)
    limGuide_.process(gl, gr, n);     // limiter da Guia

    const auto mode = static_cast<RoutingMode>(routing_.load(std::memory_order_relaxed));
    const float masterTarget = masterGain_.load(std::memory_order_relaxed);
    const int32_t nch = outChannels_;
    const int32_t mapCount = chanMapCount_.load(std::memory_order_acquire);
    int32_t map[kMaxOutChannels];
    for (int32_t c = 0; c < mapCount; ++c) map[c] = chanMap_[c].load(std::memory_order_relaxed);
    for (int32_t i = 0; i < n; ++i) {
        masterSmoothed_ += (masterTarget - masterSmoothed_) * kMasterSmoothCoef;
        const float click = 0.5f * (cl[i] + cr[i]);
        const float guide = 0.5f * (gl[i] + gr[i]);
        const float cue = click + guide;
        const float bandMono = 0.5f * (bl[i] + br[i]);
        float* o = out + static_cast<size_t>(i) * static_cast<size_t>(nch);
        // Mapa só vale se o stream tem canais suficientes; senão (ex.: USB desconectado -> stream estéreo
        // padrão) cai no roteamento clássico, para o Click nunca sumir.
        if ((mapCount > 0 && nch >= mapCount) || nch > 2) {
            for (int32_t c = 0; c < nch; ++c) {
                float v = 0.f;
                switch (c < mapCount ? static_cast<OutSrc>(map[c]) : OutSrc::Off) {
                    case OutSrc::BandL: v = bl[i]; break;
                    case OutSrc::BandR: v = br[i]; break;
                    case OutSrc::BandMono: v = bandMono; break;
                    case OutSrc::Click: v = click; break;
                    case OutSrc::Guide: v = guide; break;
                    case OutSrc::CueMix: v = cue; break;
                    case OutSrc::FullL: v = bl[i] + cue; break;
                    case OutSrc::FullR: v = br[i] + cue; break;
                    default: break;
                }
                o[c] = std::clamp(v * masterSmoothed_, -1.f, 1.f);
            }
        } else if (nch == 1) {
            o[0] = std::clamp((bandMono + cue) * masterSmoothed_, -1.f, 1.f);
        } else {
            float l, r;
            if (mode == RoutingMode::SplitCueLeftBandRight) { l = cue; r = bandMono; }
            else { l = bl[i] + cue; r = br[i] + cue; }
            o[0] = std::clamp(l * masterSmoothed_, -1.f, 1.f);
            o[1] = std::clamp(r * masterSmoothed_, -1.f, 1.f);
        }
    }
}

}  // namespace vs
