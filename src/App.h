#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Library.h"
#include "core/Midi.h"
#include "core/Player.h"

struct SDL_Window;

namespace vss {

namespace fs = std::filesystem;

enum class Tab { Stage = 0, Library, Setlists, Output, Pedals, Count };

struct ImportDraft {
    bool open = false;
    int64_t addToSong = -1;                 // >=0: adicionar faixas a uma música existente
    std::vector<fs::path> files;
    std::vector<int> roles;                 // Role por arquivo
    std::vector<std::string> names;
    char title[160] = {0};
};

class App {
public:
    App(SDL_Window* window, const fs::path& home);
    ~App();

    void frame();                           // desenha a interface (chamado entre NewFrame e Render)
    void onDrop(const std::string& utf8Path);
    void setTab(Tab t) { tab_ = t; }
    Tab tab() const { return tab_; }
    bool wantQuit() const { return quit_; }
    Player& player() { return player_; }
    Library& library() { return lib_; }
    void seedDemo();                        // cria e carrega a música de demonstração
    void loadSong(int64_t id);
    void toast(const std::string& msg);
    void setUiScale(float s) { dpi_ = s; }
    /// Gancho para as capturas de tela automáticas: 0 = editor de seções, 1 = importação.
    void debugShowModal(int which);

private:
    // --- telas ---
    void drawTopBar(float w);
    void drawStage(float w, float h);
    void drawStageLeft(float w, float h);
    void drawSections(float w);
    void drawSeekBar(float w);
    void drawMixer(float w, float h);
    void drawLibrary(float w, float h);
    void drawSetlists(float w, float h);
    void drawOutput(float w, float h);
    void drawPedals(float w, float h);
    void drawModals();
    void drawSectionEditor();
    void drawToast();

    // --- lógica ---
    void handleAction(Action a);
    void pollMidi();
    void handleShortcuts();
    void onSongLoaded();
    void persistSong();                     // grava BPM/offset/seções detectados ou editados
    void loadSetlistItem(int64_t setlistId, int index);
    void stepSetlist(int delta);
    void saveItemAdjustments();             // grava ajustes do dia no item do setlist atual
    void openImport(std::vector<fs::path> files, int64_t addTo = -1);
    void restartAudio();
    void refreshDevices();
    std::string transposedKey(const std::string& key, int semis) const;

    SDL_Window* window_;
    void* hwnd_ = nullptr;
    fs::path home_;
    Library lib_;
    Player player_;
    MidiInput midi_;
    float dpi_ = 1.f;
    Tab tab_ = Tab::Stage;
    bool quit_ = false;

    // setlist ativo
    int64_t curSetlist_ = -1;
    int curIndex_ = -1;

    // seleção nas telas
    int64_t selSong_ = -1, selSetlist_ = -1;
    ImportDraft draft_;
    std::string toast_;
    std::chrono::steady_clock::time_point toastUntil_;
    bool openSectionEditor_ = false;
    std::vector<Section> editSections_;
    bool confirmDeleteSong_ = false, confirmDeleteSetlist_ = false;
    std::vector<fs::path> pendingDrops_;

    // saída
    std::vector<OutputDeviceInfo> devices_;
    bool devicesLoaded_ = false;
    Settings editSettings_;
    bool settingsInit_ = false;
    StreamInfo streamInfo_;
    std::chrono::steady_clock::time_point lastInfo_;
    bool audioOk_ = false;

    // MIDI
    std::vector<std::string> midiPorts_;
    bool learning_ = false;
    Action learnAction_ = Action::PlayPause;
    std::string lastMidiLabel_;
    bool libDirty_ = false;
    bool importOpen_ = false;
    bool closeModals_ = false;
    bool wasPlayingBeforeEdit_ = false;
};

}  // namespace vss
