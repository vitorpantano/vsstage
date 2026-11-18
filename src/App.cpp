#include "App.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Demo.h"
#include "FileDialog.h"
#include "Ui.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace vss {
using namespace ui;

App::App(SDL_Window* window, const fs::path& home) : window_(window), home_(home), lib_(home) {
#if defined(_WIN32)
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window, &wm)) hwnd_ = wm.info.win.window;
#endif
    lib_.load();
    editSettings_ = lib_.settings;
    settingsInit_ = true;
    audioOk_ = player_.startAudio(lib_.settings);
    if (!audioOk_) toast("Sem saída de áudio: " + player_.lastError + " (veja a aba Saída)");
    player_.applyMaster(lib_.settings);
    midiPorts_ = midi_.ports();
    if (!lib_.settings.midiInput.empty()) midi_.open(lib_.settings.midiInput);
}

App::~App() {
    persistSong();
    lib_.save();
}

void App::toast(const std::string& msg) {
    toast_ = msg;
    toastUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
}

// ------------------------------------------------------------------ sessão

void App::loadSong(int64_t id) {
    SongEntry* s = lib_.song(id);
    if (!s) return;
    persistSong();
    curSetlist_ = -1; curIndex_ = -1;
    player_.loadSong(lib_, *s, [this] { onSongLoaded(); });
}

void App::loadSetlistItem(int64_t setlistId, int index) {
    Setlist* sl = lib_.setlist(setlistId);
    if (!sl || index < 0 || index >= static_cast<int>(sl->items.size())) return;
    SongEntry* s = lib_.song(sl->items[static_cast<size_t>(index)].songId);
    if (!s) { toast("Música do item não existe mais"); return; }
    persistSong();
    curSetlist_ = setlistId; curIndex_ = index;
    player_.loadSong(lib_, *s, [this] { onSongLoaded(); });
}

void App::stepSetlist(int delta) {
    if (curSetlist_ < 0) return;
    Setlist* sl = lib_.setlist(curSetlist_);
    if (!sl) return;
    const int n = static_cast<int>(sl->items.size());
    const int next = curIndex_ + delta;
    if (next < 0 || next >= n) { toast(delta > 0 ? "Fim do setlist" : "Início do setlist"); return; }
    saveItemAdjustments();
    loadSetlistItem(curSetlist_, next);
}

void App::onSongLoaded() {
    if (curSetlist_ >= 0) {
        if (Setlist* sl = lib_.setlist(curSetlist_)) {
            if (curIndex_ >= 0 && curIndex_ < static_cast<int>(sl->items.size())) {
                const SetlistItem& it = sl->items[static_cast<size_t>(curIndex_)];
                player_.setTranspose(it.transpose);
                if (it.targetBpm > 0) player_.setTargetBpm(it.targetBpm);
                for (size_t i = 0; i < player_.tracks.size(); ++i) {
                    if (it.muted.count(player_.tracks[i].name)) player_.setMute(i, true);
                    auto g = it.gains.find(player_.tracks[i].name);
                    if (g != it.gains.end()) player_.setGain(i, g->second);
                }
                if (it.padKey >= 0) { player_.setPadGain(it.padGain); player_.padSelect(it.padKey); }
            }
        }
    }
    // música com faixa com erro: avisa
    for (auto& t : player_.tracks)
        if (t.failed) { toast("Falha ao carregar \"" + t.name + "\": " + t.error); break; }
    persistSong();
}

void App::persistSong() {
    if (player_.songId < 0 || player_.pendingLoads() > 0) return;
    SongEntry* s = lib_.song(player_.songId);
    if (!s) return;
    bool changed = false;
    if (player_.analyzedBpmPending || player_.mapDirty) {
        s->bpm = player_.map.bpm; s->offsetMs = player_.map.offsetMs; s->beatsPerBar = player_.map.beatsPerBar;
        s->analyzed = true;
        if (player_.hasCustomSections) s->sections = player_.map.sections;
        player_.analyzedBpmPending = false; player_.mapDirty = false;
        changed = true;
    }
    if (changed) lib_.save();
}

void App::saveItemAdjustments() {
    if (curSetlist_ < 0 || player_.songId < 0) return;
    Setlist* sl = lib_.setlist(curSetlist_);
    if (!sl || curIndex_ < 0 || curIndex_ >= static_cast<int>(sl->items.size())) return;
    SetlistItem& it = sl->items[static_cast<size_t>(curIndex_)];
    it.transpose = player_.transposeSemis;
    it.targetBpm = player_.targetBpm;
    it.padKey = player_.padKey;
    it.padGain = player_.padGain;
    it.muted.clear(); it.gains.clear();
    for (auto& t : player_.tracks) {
        if (t.mute) it.muted.insert(t.name);
        if (std::abs(t.gain - 1.0f) > 0.001f) it.gains[t.name] = t.gain;
    }
    lib_.save();
}

// ------------------------------------------------------------------ importação

void App::openImport(std::vector<fs::path> files, int64_t addTo) {
    if (files.empty()) return;
    draft_ = ImportDraft{};
    draft_.open = true;
    draft_.addToSong = addTo;
    draft_.files = std::move(files);
    std::vector<std::string> names;
    for (auto& f : draft_.files) { auto u = f.filename().u8string(); names.emplace_back(u.begin(), u.end()); }
    MappedSong m = mapper::analyze(names, "Nova música");
    for (auto& f : m.files) { draft_.roles.push_back(static_cast<int>(f.role)); draft_.names.push_back(f.displayName); }
    const std::string title = addTo >= 0 && lib_.song(addTo) ? lib_.song(addTo)->title : m.title;
    std::strncpy(draft_.title, title.c_str(), sizeof(draft_.title) - 1);
    tab_ = Tab::Library;
}

void App::debugShowModal(int which) {
    closeModals_ = true;
    if (which == 0) { editSections_ = player_.map.sections; openSectionEditor_ = true; }
    else if (which == 1) { openImport(generateDemoSong(home_ / "demo_audio_2")); }
}

void App::onDrop(const std::string& path) { pendingDrops_.push_back(fs::u8path(path)); }

void App::seedDemo() {
    auto files = generateDemoSong(home_ / "demo_audio");
    std::string err;
    const int64_t id = lib_.importSong(files, "Demo Louvor", &err);
    if (id < 0) { toast(err); return; }
    if (SongEntry* s = lib_.song(id)) { s->artist = "VS Stage"; s->key = "C"; }
    lib_.save();
    selSong_ = id;
    const int64_t slId = lib_.newSetlist("Culto de domingo");
    if (Setlist* sl = lib_.setlist(slId)) {
        SetlistItem a; a.songId = id; sl->items.push_back(a);
        SetlistItem b; b.songId = id; b.transpose = 2; b.targetBpm = 104; b.padKey = 2; sl->items.push_back(b);
        sl->dateLabel = "Dom, 19h";
    }
    lib_.save();
    selSetlist_ = slId;
    loadSong(id);
}

// ------------------------------------------------------------------ MIDI e atalhos

static bool keyToBinding(const std::string& key, MidiBinding* b) {
    int ch = 0, num = 0;
    char t = 0;
    if (std::sscanf(key.c_str(), "%c:%d:%d", &t, &ch, &num) != 3) return false;
    b->type = t == 'N' ? 0 : (t == 'C' ? 1 : 2); b->channel = ch; b->number = num;
    return true;
}
static bool sameTrigger(const MidiBinding& a, const MidiBinding& b) { return a.type == b.type && a.channel == b.channel && a.number == b.number; }

void App::pollMidi() {
    for (auto& t : midi_.poll()) {
        lastMidiLabel_ = t.label;
        MidiBinding b;
        if (!keyToBinding(t.key, &b)) continue;
        if (learning_) {
            b.action = actionId(learnAction_);
            auto& v = lib_.settings.midi;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const MidiBinding& x) { return x.action == b.action || sameTrigger(x, b); }), v.end());
            v.push_back(b);
            lib_.save();
            learning_ = false;
            toast(std::string("Pedal ligado a: ") + actionLabel(learnAction_));
            continue;
        }
        for (auto& m : lib_.settings.midi) {
            if (sameTrigger(m, b)) { Action a; if (actionFromId(m.action, &a)) handleAction(a); break; }
        }
    }
}

void App::handleAction(Action a) {
    switch (a) {
        case Action::PlayPause: player_.togglePlay(); break;
        case Action::Stop: player_.stop(); break;
        case Action::NextSection: player_.jumpRelativeSection(+1); break;
        case Action::PrevSection: player_.jumpRelativeSection(-1); break;
        case Action::LoopSection: {
            const int cur = std::max(0, player_.currentSection());
            player_.setLoopSection(cur, !player_.loopEnabled());
            break;
        }
        case Action::TransposeUp: player_.setTranspose(player_.transposeSemis + 1); break;
        case Action::TransposeDown: player_.setTranspose(player_.transposeSemis - 1); break;
        case Action::BpmUp: player_.stepBpm(+1); break;
        case Action::BpmDown: player_.stepBpm(-1); break;
        case Action::PadNext: player_.padSelect(player_.padKey < 0 ? 0 : (player_.padKey + 1) % 12); break;
        case Action::PadOff: player_.padSelect(-1); break;
        case Action::NextSong: stepSetlist(+1); break;
        case Action::PrevSong: stepSetlist(-1); break;
        default: break;
    }
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || tab_ != Tab::Stage) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) handleAction(Action::PlayPause);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) handleAction(Action::NextSection);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) handleAction(Action::PrevSection);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) handleAction(io.KeyShift ? Action::TransposeUp : Action::BpmUp);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) handleAction(io.KeyShift ? Action::TransposeDown : Action::BpmDown);
    if (ImGui::IsKeyPressed(ImGuiKey_L, false)) handleAction(Action::LoopSection);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) handleAction(Action::NextSong);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) handleAction(Action::PrevSong);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) handleAction(Action::Stop);
}

// ------------------------------------------------------------------ áudio

void App::refreshDevices() {
    devices_ = player_.audio().listDevices();
    devicesLoaded_ = true;
}

void App::restartAudio() {
    const bool wasPlaying = player_.playing();
    const int64_t pos = player_.position();
    lib_.settings = editSettings_;
    audioOk_ = player_.startAudio(lib_.settings);
    lib_.save();
    if (!audioOk_) toast("Não foi possível abrir a saída: " + player_.lastError);
    else { toast("Saída de áudio aplicada"); if (player_.songId >= 0) player_.seekFrame(pos); if (wasPlaying) player_.play(); }
}

std::string App::transposedKey(const std::string& key, int semis) const {
    if (key.empty()) return "";
    static const char* sharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    static const int base[] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    const char c = key[0];
    if (c < 'A' || c > 'G') return key;
    int n = base[c - 'A'];
    size_t i = 1;
    bool useFlat = false;
    if (i < key.size() && key[i] == '#') { ++n; ++i; }
    else if (i < key.size() && key[i] == 'b') { --n; ++i; useFlat = true; }
    n = ((n + semis) % 12 + 12) % 12;
    if (semis < 0) useFlat = true;
    return std::string(useFlat ? flat[n] : sharp[n]) + key.substr(i);
}

// ------------------------------------------------------------------ quadro

void App::frame() {
    player_.update();
    pollMidi();
    for (auto& d : pendingDrops_) {
        std::error_code ec;
        if (d.extension() == ".vsstage") {
            std::string err; int64_t sl = -1; int n = 0;
            if (lib_.importPackage(d, &sl, &n, &err)) { toast("Pacote importado: " + std::to_string(n) + " música(s)"); tab_ = sl >= 0 ? Tab::Setlists : Tab::Library; if (sl >= 0) selSetlist_ = sl; }
            else toast(err);
        } else if (fs::is_directory(d, ec)) {
            openImport(dlg::audioFilesIn(d));
        } else if (dlg::isAudioFile(d)) {
            if (!draft_.open) openImport({d});
            else { draft_.files.push_back(d); auto u = d.filename().u8string(); MappedSong m = mapper::analyze({std::string(u.begin(), u.end())}); draft_.roles.push_back(static_cast<int>(m.files[0].role)); draft_.names.push_back(m.files[0].displayName); }
        }
    }
    pendingDrops_.clear();
    handleShortcuts();
    if (closeModals_) { if (!ImGui::GetCurrentContext()->OpenPopupStack.empty()) ImGui::ClosePopupToLevel(0, false); closeModals_ = false; }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float w = ImGui::GetContentRegionAvail().x;
    drawTopBar(w);
    const float h = ImGui::GetContentRegionAvail().y;
    switch (tab_) {
        case Tab::Stage: drawStage(w, h); break;
        case Tab::Library: drawLibrary(w, h); break;
        case Tab::Setlists: drawSetlists(w, h); break;
        case Tab::Output: drawOutput(w, h); break;
        case Tab::Pedals: drawPedals(w, h); break;
        default: break;
    }
    ImGui::End();
    drawModals();
    drawToast();
    if (libDirty_ && !ImGui::IsAnyItemActive()) { lib_.save(); libDirty_ = false; }
}

void App::drawToast() {
    if (toast_.empty() || std::chrono::steady_clock::now() > toastUntil_) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - 30 * dpi_), ImGuiCond_Always, ImVec2(0.5f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(0x2A2A34));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(toast_.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::drawTopBar(float w) {
    static const char* names[] = {"Palco", "Biblioteca", "Setlists", "Saída", "Pedais"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float barH = 46 * dpi_;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(p.x + 4, p.y + barH * 0.5f - ImGui::GetFontSize() * 0.5f), col32(kAccent), "VS STAGE");
    ImGui::Dummy(ImVec2(112 * dpi_, barH - 8 * dpi_));
    for (int i = 0; i < static_cast<int>(Tab::Count); ++i) {
        ImGui::SameLine();
        const bool on = static_cast<int>(tab_) == i;
        if (ToggleButton(names[i], on, ImVec2(112 * dpi_, barH - 8 * dpi_), kAccent)) tab_ = static_cast<Tab>(i);
    }
    // status à direita
    char buf[160];
    const auto now = std::chrono::steady_clock::now();
    if (now - lastInfo_ > std::chrono::milliseconds(400)) { streamInfo_ = player_.audio().info(); lastInfo_ = now; }
    if (streamInfo_.open) std::snprintf(buf, sizeof buf, "%d ch · %.0f ms · CPU %.0f%%", streamInfo_.channels, streamInfo_.latencyMs, streamInfo_.cpuAvg * 100.f);
    else std::snprintf(buf, sizeof buf, "Sem saída de áudio");
    const float tw = ImGui::CalcTextSize(buf).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), w - tw - 6));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, col(streamInfo_.open ? (streamInfo_.cpuAvg > 0.7f ? kWarn : kDim) : kBad));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));
}

}  // namespace vss
