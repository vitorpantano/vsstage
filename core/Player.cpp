#include "core/Player.h"

#include <algorithm>
#include <cmath>

#include "engine/TempoDetect.h"

namespace vss {

Player::Player() : audio_(engine_) {
    engine_.setLimiter(true, -0.3f);
}

Player::~Player() {
    audio_.stop();
    cancel_ = true;
    joinWorkers();
}

void Player::joinWorkers() {
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

// ----------------------------------------------------------------- carga

void Player::clear() {
    ++gen_;
    cancel_ = true;
    joinWorkers();
    cancel_ = false;
    { std::lock_guard<std::mutex> l(qm_); done_.clear(); }
    pending_ = 0;
    engine_.stop();
    engine_.removeAllTracks();
    engine_.padSelect(-1);
    tracks.clear();
    songId = -1; title.clear();
    map = SongMap{};
    autoSections_.clear();
    mapDirty = false; analyzedBpmPending = false; hasCustomSections = false;
    transposeSemis = 0; targetBpm = 0; padKey = -1; loopSection_ = -1;
    engine_.setTranspose(0);
    engine_.setTempoRatio(1.0);
    engine_.setLoop(0, 0, false);
    finished_ = true;
}

void Player::loadSong(const Library& lib, const SongEntry& song, const std::function<void()>& onLoaded) {
    clear();
    songId = song.id;
    title = song.title;
    map.bpm = song.bpm > 20 ? song.bpm : 120.0;
    map.beatsPerBar = std::max(1, song.beatsPerBar);
    map.offsetMs = song.offsetMs;
    map.sections = song.sections;
    hasCustomSections = !song.sections.empty();
    analyzedBpmPending = !song.analyzed;
    onLoaded_ = onLoaded;
    finished_ = false;
    lastError.clear();

    std::vector<std::pair<int, std::filesystem::path>> jobs;
    for (size_t i = 0; i < song.tracks.size(); ++i) {
        const TrackEntry& te = song.tracks[i];
        PlayerTrack t;
        t.name = te.name; t.file = te.file;
        t.role = roleFromId(te.role);
        const RoleInfo& ri = roleInfo(t.role);
        t.bus = ri.bus; t.pitchLock = ri.pitchLock; t.gain = te.gain;
        tracks.push_back(t);
        jobs.emplace_back(static_cast<int>(i), lib.trackPath(song, te));
    }
    if (jobs.empty()) { finishLoad(); return; }
    pending_ = static_cast<int>(jobs.size());
    applyMap();
    startWorkers(std::move(jobs), gen_.load());
}

void Player::startWorkers(std::vector<std::pair<int, std::filesystem::path>> jobs, uint64_t gen) {
    const unsigned hw = std::max(2u, std::min(4u, std::thread::hardware_concurrency()));
    auto next = std::make_shared<std::atomic<size_t>>(0);
    auto list = std::make_shared<std::vector<std::pair<int, std::filesystem::path>>>(std::move(jobs));
    const bool needDetect = analyzedBpmPending;
    for (unsigned w = 0; w < std::min<size_t>(hw, list->size()); ++w) {
        workers_.emplace_back([this, next, list, gen, needDetect]() {
            for (;;) {
                const size_t k = next->fetch_add(1);
                if (k >= list->size() || cancel_.load()) break;
                Loaded l; l.gen = gen; l.idx = (*list)[k].first;
                l.buf = decodeFile((*list)[k].second, &l.err);
                if (l.buf && needDetect && !cancel_.load()) {
                    auto r = vs::detectTempo(l.buf->pcm.data(), l.buf->frames, vs::kEngineSampleRate);
                    l.bpm = r.bpm; l.off = r.offsetSec; l.conf = r.confidence;
                }
                std::lock_guard<std::mutex> g(qm_);
                done_.push_back(std::move(l));
            }
        });
    }
}

void Player::installLoaded(Loaded& l) {
    if (l.gen != gen_.load() || l.idx < 0 || l.idx >= static_cast<int>(tracks.size())) return;
    PlayerTrack& t = tracks[static_cast<size_t>(l.idx)];
    t.loading = false;
    if (!l.buf) {
        t.failed = true; t.error = l.err;
        lastError = l.err;
    } else {
        const RoleInfo& ri = roleInfo(t.role);
        const int slot = engine_.installTrack(std::move(l.buf), t.bus, t.gain, ri.soloSafe, t.pitchLock);
        if (slot < 0) { t.failed = true; t.error = "Limite de faixas atingido"; }
        else { t.slot = slot; t.detectedBpm = l.bpm; t.detectedOffsetSec = l.off; t.detectedConf = l.conf; }
    }
    if (--pending_ == 0) finishLoad();
}

void Player::finishLoad() {
    if (finished_) return;
    finished_ = true;
    // Detecção automática: prefere o Click; senão a faixa de maior confiança.
    if (analyzedBpmPending) applyDetected(false);
    if (!hasCustomSections) {
        map.sections = SongMap::automatic(engine_.durationFrames(), map);
    } else {
        map.sections = SongMap::normalize(map.sections, map.totalBars(engine_.durationFrames()));
    }
    applyMap();
    if (onLoaded_) onLoaded_();
}

void Player::applyDetected(bool force) {
    const PlayerTrack* best = nullptr;
    for (auto& t : tracks) {
        if (t.detectedBpm <= 0 || t.detectedConf < 0.2) continue;
        if (!best) { best = &t; continue; }
        const bool bc = best->role == Role::Click, tc = t.role == Role::Click;
        if (tc && !bc) best = &t;
        else if (tc == bc && t.detectedConf > best->detectedConf) best = &t;
    }
    if (!best) return;
    if (force || analyzedBpmPending) {
        map.bpm = std::round(best->detectedBpm * 10.0) / 10.0;
        map.offsetMs = best->detectedOffsetSec * 1000.0;
        mapDirty = true;
    }
}

void Player::applyMap() {
    engine_.setGrid(map.bpm, map.beatsPerBar, map.offsetFrames());
    recomputeTempo();
}

int Player::totalBars() const { return map.totalBars(engine_.durationFrames()); }

void Player::update() {
    for (int guard = 0; guard < 64; ++guard) {
        Loaded l;
        {
            std::lock_guard<std::mutex> g(qm_);
            if (done_.empty()) break;
            l = std::move(done_.front());
            done_.pop_front();
        }
        installLoaded(l);
    }
    for (auto& t : tracks) {
        if (t.slot < 0) continue;
        const float p = engine_.consumeTrackPeak(t.slot);
        t.peak = std::max(p, t.peak * 0.88f);
    }
    limiterReductionDb = engine_.takeLimiterReductionDb();
}

// ----------------------------------------------------------------- transporte

void Player::play() { engine_.play(); }
void Player::pause() { engine_.pause(); }
void Player::stop() { engine_.stop(); }
void Player::togglePlay() { if (engine_.isPlaying() || engine_.isStarting()) engine_.pause(); else engine_.play(); }
void Player::seekFrame(int64_t f) { engine_.seek(std::max<int64_t>(0, f)); }

int Player::currentSection() const { return map.sectionIndexAt(position()); }

int Player::pendingSection() const {
    const int64_t t = engine_.pendingJumpTarget();
    if (t < 0) return -1;
    return map.sectionIndexAt(t);
}

void Player::jumpToSection(int index, vs::Quantize q) {
    if (index < 0 || index >= static_cast<int>(map.sections.size())) return;
    const int64_t target = map.sectionStart(map.sections[static_cast<size_t>(index)]);
    if (!engine_.isPlaying() && !engine_.isStarting()) engine_.seek(target);
    else engine_.jumpTo(target, q);
}

void Player::jumpRelativeSection(int delta) {
    if (map.sections.empty()) return;
    const int base = pendingSection() >= 0 ? pendingSection() : std::max(0, currentSection());
    const int n = static_cast<int>(map.sections.size());
    jumpToSection(std::clamp(base + delta, 0, n - 1));
}

void Player::setLoopSection(int index, bool enabled) {
    if (!enabled || index < 0 || index >= static_cast<int>(map.sections.size())) {
        loopSection_ = -1;
        engine_.setLoop(0, 0, false);
        return;
    }
    loopSection_ = index;
    const Section& s = map.sections[static_cast<size_t>(index)];
    engine_.setLoop(map.sectionStart(s), map.sectionEnd(s), true);
}

// ----------------------------------------------------------------- faixas

#define VALID(i) ((i) < tracks.size() && tracks[i].slot >= 0)
void Player::setGain(size_t i, float g) { if (i < tracks.size()) { tracks[i].gain = std::clamp(g, 0.f, vs::kMaxTrackGain); if (VALID(i)) engine_.setTrackGain(tracks[i].slot, tracks[i].gain); } }
void Player::setMute(size_t i, bool m) { if (i < tracks.size()) { tracks[i].mute = m; if (VALID(i)) engine_.setTrackMute(tracks[i].slot, m); } }
void Player::setSolo(size_t i, bool s) { if (i < tracks.size()) { tracks[i].solo = s; if (VALID(i)) engine_.setTrackSolo(tracks[i].slot, s); } }
void Player::setPitchLock(size_t i, bool l) { if (i < tracks.size()) { tracks[i].pitchLock = l; if (VALID(i)) engine_.setTrackPitchLock(tracks[i].slot, l); } }
void Player::setBus(size_t i, vs::Bus b) { if (i < tracks.size()) { tracks[i].bus = b; if (VALID(i)) engine_.setTrackBus(tracks[i].slot, b); } }
void Player::moveTrack(size_t from, size_t to) {
    if (from >= tracks.size() || to >= tracks.size() || from == to) return;
    PlayerTrack t = tracks[from];
    tracks.erase(tracks.begin() + static_cast<long>(from));
    tracks.insert(tracks.begin() + static_cast<long>(to), t);
}

// ----------------------------------------------------------------- tom / andamento

void Player::setTranspose(int semis) {
    transposeSemis = std::clamp(semis, -6, 6);
    engine_.setTranspose(transposeSemis);
}

void Player::recomputeTempo() {
    double ratio = 1.0;
    if (targetBpm > 0 && map.bpm > 0) ratio = std::clamp(targetBpm / map.bpm, vs::kMinTempo, vs::kMaxTempo);
    engine_.setTempoRatio(ratio);
}
void Player::setTargetBpm(double bpm) {
    targetBpm = bpm > 0 ? std::clamp(bpm, map.bpm * vs::kMinTempo, map.bpm * vs::kMaxTempo) : 0.0;
    if (std::abs(targetBpm - map.bpm) < 0.01) targetBpm = 0.0;
    recomputeTempo();
}
void Player::stepBpm(double delta) { setTargetBpm(std::round((currentBpm() + delta) * 10.0) / 10.0); }

void Player::setMasterGain(float g) { masterGain = std::clamp(g, 0.f, 1.5f); engine_.setMasterGain(masterGain); }
void Player::applyMaster(const Settings& s) {
    engine_.setEq(s.eqLow, s.eqMid, s.eqHigh);
    engine_.setLimiter(s.masterLimiter, -0.3f);
    setMasterGain(s.masterGain);
}

// ----------------------------------------------------------------- pad

void Player::padSelect(int key) {
    padKey = key;
    if (key >= 0 && !engine_.hasPad(key)) engine_.generateSynthPad(key);
    engine_.padSelect(key);
}
void Player::setPadGain(float g) { padGain = std::clamp(g, 0.f, 1.f); engine_.padSetGain(padGain); }

// ----------------------------------------------------------------- seções

void Player::setSections(std::vector<Section> s) {
    map.sections = SongMap::normalize(std::move(s), map.totalBars(engine_.durationFrames()));
    hasCustomSections = true;
    mapDirty = true;
    if (loopSection_ >= 0) setLoopSection(std::min(loopSection_, static_cast<int>(map.sections.size()) - 1), true);
}

void Player::markSectionHere() {
    const int bar = std::max(0, map.barAt(position()));
    auto list = map.sections;
    const int n = static_cast<int>(list.size());
    Section s;
    s.name = SongMap::names()[static_cast<size_t>(n) % SongMap::names().size()];
    s.colorRgb = SongMap::colors()[static_cast<size_t>(n) % SongMap::colors().size()];
    s.startBar = bar;
    list.push_back(s);
    setSections(std::move(list));
}

// ----------------------------------------------------------------- saída

bool Player::startAudio(const Settings& s) {
    int idx = -1;
    if (!s.deviceName.empty()) {
        auto devs = audio_.listDevices();
        for (size_t i = 0; i < devs.size(); ++i) if (devs[i].name == s.deviceName) idx = static_cast<int>(i);
    }
    applyChannelMap(s.channelMap);
    applyRoutingMode(s.routing);
    applyMaster(s);
    const bool ok = audio_.start(idx, s.channels, s.periodFrames, s.exclusive);
    if (!ok) lastError = audio_.lastError();
    return ok;
}

void Player::applyChannelMap(const std::vector<int>& m) {
    std::vector<int32_t> v(m.begin(), m.end());
    engine_.setChannelMap(v.data(), static_cast<int32_t>(v.size()));
}
void Player::applyRoutingMode(int mode) {
    engine_.setRoutingMode(mode == 0 ? vs::RoutingMode::Stereo : vs::RoutingMode::SplitCueLeftBandRight);
}

}  // namespace vss
