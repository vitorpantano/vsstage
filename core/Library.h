#pragma once
// Biblioteca local: músicas, setlists e configurações em JSON dentro de %APPDATA%\VSStage (ou $VSSTAGE_HOME).
// Pacotes .vsstage são zips compatíveis com os do app Android (manifest.json + audio/<ref>/<arquivo>).
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Mapper.h"
#include "core/Types.h"

namespace vss {

namespace fs = std::filesystem;

struct TrackEntry {
    std::string name;
    std::string file;          // nome do arquivo dentro da pasta da música
    std::string role = "OTHER";
    float gain = 1.0f;
};

struct SongEntry {
    int64_t id = 0;
    std::string title, artist, key;
    double bpm = 120.0;
    int beatsPerBar = 4;
    double offsetMs = 0.0;
    bool analyzed = false;
    std::vector<Section> sections;
    std::vector<TrackEntry> tracks;
};

struct SetlistItem {
    int64_t songId = 0;
    int transpose = 0;
    double targetBpm = 0.0;     // 0 = BPM original
    int padKey = -1;            // -1 = sem pad
    float padGain = 0.8f;
    std::set<std::string> muted;               // nomes de faixas mudas neste dia
    std::map<std::string, float> gains;        // ganhos por faixa neste dia
    std::string notes;
};

struct Setlist {
    int64_t id = 0;
    std::string name, dateLabel, notes;
    std::vector<SetlistItem> items;
};

struct MidiBinding { std::string action; int type = 0; int channel = 0; int number = 0; };   // type: 0=nota, 1=CC, 2=PC

struct Settings {
    std::string deviceName;          // vazio = padrão do sistema
    int channels = 2;
    int periodFrames = 256;
    bool exclusive = false;
    std::vector<int> channelMap;     // vazio = roteamento estéreo padrão (OutSrc por canal)
    int routing = 1;                 // RoutingMode
    std::string midiInput;           // vazio = nenhum
    std::vector<MidiBinding> midi;
    bool masterLimiter = true;
    float masterGain = 1.0f;
    float eqLow = 0, eqMid = 0, eqHigh = 0;
};

class Library {
public:
    explicit Library(fs::path root);
    static fs::path defaultRoot();

    const fs::path& root() const { return root_; }
    fs::path songDir(int64_t id) const { return root_ / "songs" / std::to_string(id); }
    fs::path trackPath(const SongEntry& s, const TrackEntry& t) const { return songDir(s.id) / fs::u8path(t.file); }

    std::vector<SongEntry> songs;
    std::vector<Setlist> setlists;
    Settings settings;

    bool load();
    bool save();

    SongEntry* song(int64_t id);
    Setlist* setlist(int64_t id);
    int64_t nextId() { return ++lastId_; }

    /// Copia os arquivos para a pasta da música e cria o registro usando as regras do SmartMapper.
    int64_t importSong(const std::vector<fs::path>& files, const std::string& title, std::string* err = nullptr);
    /// Adiciona faixas a uma música existente.
    bool addTracks(int64_t songId, const std::vector<fs::path>& files, std::string* err = nullptr);
    void removeSong(int64_t id);
    bool removeTrack(int64_t songId, size_t index);

    int64_t newSetlist(const std::string& name);
    void removeSetlist(int64_t id);
    int64_t duplicateSetlist(int64_t id);

    /// Exporta um pacote .vsstage. setlistId < 0: exporta songIds sem setlist.
    bool exportPackage(const fs::path& out, int64_t setlistId, std::vector<int64_t> songIds, bool includeAudio, std::string* err = nullptr);
    /// Importa um pacote e devolve o id do setlist criado (-1 se não havia) via outSetlist.
    bool importPackage(const fs::path& pkg, int64_t* outSetlist = nullptr, int* outSongs = nullptr, std::string* err = nullptr);

private:
    fs::path root_;
    int64_t lastId_ = 0;
};

}  // namespace vss
