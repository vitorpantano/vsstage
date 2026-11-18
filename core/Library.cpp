#include "core/Library.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "json/json.hpp"
#include "miniz/miniz.h"

using json = nlohmann::json;

namespace vss {

namespace {

std::string u8(const fs::path& p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }

FILE* openFile(const fs::path& p, const char* mode) {
#if defined(_WIN32)
    std::wstring m; for (const char* c = mode; *c; ++c) m.push_back(static_cast<wchar_t>(*c));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

bool writeTextAtomic(const fs::path& p, const std::string& text) {
    fs::path tmp = p; tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(p, ec); fs::rename(tmp, p, ec); }
    return !ec;
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string safeFileName(std::string s) {
    for (auto& c : s) if (std::string("\\/:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
    return s.empty() ? "arquivo" : s;
}

std::string sectionsToString(const std::vector<Section>& v) {
    json a = json::array();
    for (auto& s : v) a.push_back({{"n", s.name}, {"c", static_cast<int64_t>(0xFF000000u | s.colorRgb)}, {"s", s.startBar}, {"b", s.bars}});
    return a.dump();
}
std::vector<Section> sectionsFromString(const std::string& str) {
    std::vector<Section> out;
    if (str.empty()) return out;
    auto a = json::parse(str, nullptr, false);
    if (!a.is_array()) return out;
    for (auto& o : a) {
        if (!o.is_object()) continue;
        Section s;
        s.name = o.value("n", "");
        s.colorRgb = static_cast<uint32_t>(o.value("c", static_cast<int64_t>(0xFF00B0FF)) & 0xFFFFFF);
        s.startBar = o.value("s", 0);
        s.bars = o.value("b", 8);
        out.push_back(s);
    }
    return out;
}

json songToJson(const SongEntry& s) {
    json t = json::array();
    for (auto& k : s.tracks) t.push_back({{"name", k.name}, {"file", k.file}, {"role", k.role}, {"gain", k.gain}});
    return {{"id", s.id}, {"title", s.title}, {"artist", s.artist}, {"key", s.key}, {"bpm", s.bpm}, {"beatsPerBar", s.beatsPerBar},
            {"offsetMs", s.offsetMs}, {"analyzed", s.analyzed}, {"sections", sectionsToString(s.sections)}, {"tracks", t}};
}
SongEntry songFromJson(const json& j) {
    SongEntry s;
    s.id = j.value("id", int64_t(0)); s.title = j.value("title", ""); s.artist = j.value("artist", ""); s.key = j.value("key", "");
    s.bpm = j.value("bpm", 120.0); s.beatsPerBar = j.value("beatsPerBar", 4); s.offsetMs = j.value("offsetMs", 0.0); s.analyzed = j.value("analyzed", false);
    s.sections = sectionsFromString(j.value("sections", ""));
    if (j.contains("tracks") && j["tracks"].is_array())
        for (auto& k : j["tracks"]) {
            TrackEntry t; t.name = k.value("name", ""); t.file = k.value("file", ""); t.role = k.value("role", "OTHER"); t.gain = k.value("gain", 1.0f);
            s.tracks.push_back(t);
        }
    return s;
}

json itemToJson(const SetlistItem& i, const char* songKey, int64_t songVal) {
    json muted = json::array(); for (auto& m : i.muted) muted.push_back(m);
    json gains = json::object(); for (auto& g : i.gains) gains[g.first] = g.second;
    return {{songKey, songVal}, {"transpose", i.transpose}, {"targetBpm", i.targetBpm}, {"padKey", i.padKey}, {"padGain", i.padGain},
            {"muted", muted.dump()}, {"gains", gains.dump()}, {"notes", i.notes}};
}
SetlistItem itemFromJson(const json& o, const char* songKey) {
    SetlistItem i;
    i.songId = o.value(songKey, int64_t(0)); i.transpose = o.value("transpose", 0); i.targetBpm = o.value("targetBpm", 0.0);
    i.padKey = o.value("padKey", -1); i.padGain = o.value("padGain", 0.8f); i.notes = o.value("notes", "");
    auto m = json::parse(o.value("muted", "[]"), nullptr, false);
    if (m.is_array()) for (auto& x : m) if (x.is_string()) i.muted.insert(x.get<std::string>());
    auto g = json::parse(o.value("gains", "{}"), nullptr, false);
    if (g.is_object()) for (auto it = g.begin(); it != g.end(); ++it) if (it.value().is_number()) i.gains[it.key()] = it.value().get<float>();
    return i;
}

}  // namespace

fs::path Library::defaultRoot() {
    if (const char* e = std::getenv("VSSTAGE_HOME")) if (*e) return fs::u8path(e);
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) return fs::u8path(a) / "VSStage";
#endif
    if (const char* h = std::getenv("HOME")) return fs::path(h) / ".vsstage";
    return fs::path("VSStageData");
}

Library::Library(fs::path root) : root_(std::move(root)) {
    std::error_code ec;
    fs::create_directories(root_ / "songs", ec);
    fs::create_directories(root_ / "exports", ec);
}

bool Library::load() {
    songs.clear(); setlists.clear(); settings = Settings{};
    const fs::path p = root_ / "library.json";
    if (!fs::exists(p)) return true;
    json j = json::parse(readText(p), nullptr, false);
    if (!j.is_object()) {
        std::error_code ec;
        fs::copy_file(p, root_ / "library.corrompido.json", fs::copy_options::overwrite_existing, ec);
        return false;
    }
    lastId_ = j.value("lastId", int64_t(0));
    if (j.contains("songs")) for (auto& s : j["songs"]) songs.push_back(songFromJson(s));
    if (j.contains("setlists"))
        for (auto& o : j["setlists"]) {
            Setlist sl; sl.id = o.value("id", int64_t(0)); sl.name = o.value("name", ""); sl.dateLabel = o.value("dateLabel", ""); sl.notes = o.value("notes", "");
            if (o.contains("items")) for (auto& it : o["items"]) sl.items.push_back(itemFromJson(it, "songId"));
            setlists.push_back(sl);
        }
    if (j.contains("settings") && j["settings"].is_object()) {
        auto& s = j["settings"];
        settings.deviceName = s.value("deviceName", ""); settings.channels = s.value("channels", 2); settings.periodFrames = s.value("periodFrames", 256);
        settings.exclusive = s.value("exclusive", false); settings.routing = s.value("routing", 1); settings.midiInput = s.value("midiInput", "");
        settings.masterLimiter = s.value("masterLimiter", true); settings.masterGain = s.value("masterGain", 1.0f);
        settings.eqLow = s.value("eqLow", 0.f); settings.eqMid = s.value("eqMid", 0.f); settings.eqHigh = s.value("eqHigh", 0.f);
        if (s.contains("channelMap") && s["channelMap"].is_array()) for (auto& c : s["channelMap"]) settings.channelMap.push_back(c.get<int>());
        if (s.contains("midi") && s["midi"].is_array())
            for (auto& m : s["midi"]) settings.midi.push_back({m.value("action", ""), m.value("type", 0), m.value("channel", 0), m.value("number", 0)});
    }
    for (auto& s : songs) lastId_ = std::max(lastId_, s.id);
    for (auto& s : setlists) lastId_ = std::max(lastId_, s.id);
    return true;
}

bool Library::save() {
    json j;
    j["version"] = 1; j["lastId"] = lastId_;
    j["songs"] = json::array(); for (auto& s : songs) j["songs"].push_back(songToJson(s));
    j["setlists"] = json::array();
    for (auto& sl : setlists) {
        json items = json::array(); for (auto& i : sl.items) items.push_back(itemToJson(i, "songId", i.songId));
        j["setlists"].push_back({{"id", sl.id}, {"name", sl.name}, {"dateLabel", sl.dateLabel}, {"notes", sl.notes}, {"items", items}});
    }
    json midi = json::array();
    for (auto& m : settings.midi) midi.push_back({{"action", m.action}, {"type", m.type}, {"channel", m.channel}, {"number", m.number}});
    j["settings"] = {{"deviceName", settings.deviceName}, {"channels", settings.channels}, {"periodFrames", settings.periodFrames},
                     {"exclusive", settings.exclusive}, {"routing", settings.routing}, {"midiInput", settings.midiInput},
                     {"masterLimiter", settings.masterLimiter}, {"masterGain", settings.masterGain},
                     {"eqLow", settings.eqLow}, {"eqMid", settings.eqMid}, {"eqHigh", settings.eqHigh},
                     {"channelMap", settings.channelMap}, {"midi", midi}};
    return writeTextAtomic(root_ / "library.json", j.dump(1));
}

SongEntry* Library::song(int64_t id) { for (auto& s : songs) if (s.id == id) return &s; return nullptr; }
Setlist* Library::setlist(int64_t id) { for (auto& s : setlists) if (s.id == id) return &s; return nullptr; }

static bool copyInto(const fs::path& src, const fs::path& dstDir, std::string* nameOut, std::string* err) {
    std::error_code ec;
    std::string base = safeFileName(u8(src.filename()));
    fs::path dst = dstDir / fs::u8path(base);
    int n = 1;
    while (fs::exists(dst)) {
        fs::path stem = fs::u8path(base).stem(), ext = fs::u8path(base).extension();
        dst = dstDir / (stem.u8string().empty() ? fs::path("arquivo") : stem);
        dst += fs::u8path(" (" + std::to_string(++n) + ")");
        dst += ext;
    }
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { if (err) *err = "Falha ao copiar " + u8(src.filename()) + ": " + ec.message(); return false; }
    *nameOut = u8(dst.filename());
    return true;
}

int64_t Library::importSong(const std::vector<fs::path>& files, const std::string& title, std::string* err) {
    if (files.empty()) { if (err) *err = "Nenhum arquivo selecionado"; return -1; }
    std::vector<std::string> names; for (auto& f : files) names.push_back(u8(f.filename()));
    MappedSong m = mapper::analyze(names, title.empty() ? "Nova música" : title);
    SongEntry s;
    s.id = nextId();
    s.title = title.empty() ? m.title : title;
    if (m.bpm) s.bpm = *m.bpm;
    if (m.key) s.key = *m.key;
    std::error_code ec;
    fs::create_directories(songDir(s.id), ec);
    for (size_t i = 0; i < files.size(); ++i) {
        std::string stored;
        if (!copyInto(files[i], songDir(s.id), &stored, err)) { fs::remove_all(songDir(s.id), ec); return -1; }
        TrackEntry t;
        t.file = stored;
        t.role = roleInfo(m.files[i].role).id;
        t.name = m.files[i].displayName;
        s.tracks.push_back(t);
    }
    songs.push_back(s);
    save();
    return s.id;
}

bool Library::addTracks(int64_t songId, const std::vector<fs::path>& files, std::string* err) {
    SongEntry* s = song(songId);
    if (!s) return false;
    std::vector<std::string> names; for (auto& f : files) names.push_back(u8(f.filename()));
    MappedSong m = mapper::analyze(names, s->title);
    for (size_t i = 0; i < files.size(); ++i) {
        std::string stored;
        if (!copyInto(files[i], songDir(songId), &stored, err)) return false;
        s->tracks.push_back({m.files[i].displayName, stored, roleInfo(m.files[i].role).id, 1.0f});
    }
    s->analyzed = false;
    save();
    return true;
}

void Library::removeSong(int64_t id) {
    std::error_code ec;
    fs::remove_all(songDir(id), ec);
    songs.erase(std::remove_if(songs.begin(), songs.end(), [&](const SongEntry& s) { return s.id == id; }), songs.end());
    for (auto& sl : setlists)
        sl.items.erase(std::remove_if(sl.items.begin(), sl.items.end(), [&](const SetlistItem& i) { return i.songId == id; }), sl.items.end());
    save();
}

bool Library::removeTrack(int64_t songId, size_t index) {
    SongEntry* s = song(songId);
    if (!s || index >= s->tracks.size()) return false;
    std::error_code ec;
    fs::remove(trackPath(*s, s->tracks[index]), ec);
    s->tracks.erase(s->tracks.begin() + static_cast<long>(index));
    save();
    return true;
}

int64_t Library::newSetlist(const std::string& name) {
    Setlist sl; sl.id = nextId(); sl.name = name.empty() ? "Novo setlist" : name;
    setlists.push_back(sl); save();
    return sl.id;
}
void Library::removeSetlist(int64_t id) {
    setlists.erase(std::remove_if(setlists.begin(), setlists.end(), [&](const Setlist& s) { return s.id == id; }), setlists.end());
    save();
}
int64_t Library::duplicateSetlist(int64_t id) {
    Setlist* src = setlist(id);
    if (!src) return -1;
    Setlist c = *src; c.id = nextId(); c.name += " (cópia)";
    setlists.push_back(c); save();
    return c.id;
}

// ------------------------------------------------------------------ pacotes .vsstage

bool Library::exportPackage(const fs::path& out, int64_t setlistId, std::vector<int64_t> songIds, bool includeAudio, std::string* err) {
    Setlist* sl = setlistId >= 0 ? setlist(setlistId) : nullptr;
    if (sl) for (auto& i : sl->items) if (std::find(songIds.begin(), songIds.end(), i.songId) == songIds.end()) songIds.push_back(i.songId);
    json m; m["format"] = "vsstage"; m["version"] = 1; m["includesAudio"] = includeAudio;
    m["songs"] = json::array();
    for (int64_t id : songIds) {
        SongEntry* s = song(id);
        if (!s) continue;
        json sj = songToJson(*s);
        sj["ref"] = s->id; sj.erase("id");
        m["songs"].push_back(sj);
    }
    if (sl) {
        json items = json::array(); for (auto& i : sl->items) items.push_back(itemToJson(i, "songRef", i.songId));
        m["setlist"] = {{"name", sl->name}, {"dateLabel", sl->dateLabel}, {"notes", sl->notes}, {"items", items}};
    }
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    FILE* fp = openFile(out, "wb");
    if (!fp) { if (err) *err = "Não foi possível criar " + u8(out); return false; }
    mz_zip_archive zip; mz_zip_zero_struct(&zip);
    bool ok = mz_zip_writer_init_cfile(&zip, fp, 0) != 0;
    if (ok) {
        const std::string mf = m.dump(1);
        ok = mz_zip_writer_add_mem(&zip, "manifest.json", mf.data(), mf.size(), MZ_DEFAULT_LEVEL) != 0;
        if (includeAudio) {
            for (int64_t id : songIds) {
                SongEntry* s = song(id);
                if (!s || !ok) continue;
                for (auto& t : s->tracks) {
                    FILE* in = openFile(trackPath(*s, t), "rb");
                    if (!in) continue;
                    const std::string entry = "audio/" + std::to_string(id) + "/" + t.file;
                    ok = mz_zip_writer_add_cfile(&zip, entry.c_str(), in, UINT64_MAX, nullptr, nullptr, 0, MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0) != 0;
                    std::fclose(in);
                    if (!ok) break;
                }
            }
        }
        if (ok) ok = mz_zip_writer_finalize_archive(&zip) != 0;
        mz_zip_writer_end(&zip);
    }
    std::fclose(fp);
    if (!ok) { if (err) *err = "Falha ao gravar o pacote"; fs::remove(out, ec); }
    return ok;
}

namespace {
size_t writeToStream(void* opaque, mz_uint64, const void* buf, size_t n) {
    auto* f = static_cast<std::ofstream*>(opaque);
    f->write(static_cast<const char*>(buf), static_cast<std::streamsize>(n));
    return *f ? n : 0;
}
}  // namespace

bool Library::importPackage(const fs::path& pkg, int64_t* outSetlist, int* outSongs, std::string* err) {
    if (outSetlist) *outSetlist = -1;
    if (outSongs) *outSongs = 0;
    FILE* fp = openFile(pkg, "rb");
    if (!fp) { if (err) *err = "Não foi possível abrir o pacote"; return false; }
    mz_zip_archive zip; mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_cfile(&zip, fp, 0, 0)) { std::fclose(fp); if (err) *err = "Arquivo não é um pacote válido"; return false; }
    auto cleanup = [&]() { mz_zip_reader_end(&zip); std::fclose(fp); };
    int mi = mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, 0);
    if (mi < 0) { cleanup(); if (err) *err = "Pacote sem manifest.json"; return false; }
    mz_zip_archive_file_stat st;
    mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(mi), &st);
    if (st.m_uncomp_size > 64u * 1024 * 1024) { cleanup(); if (err) *err = "Manifesto grande demais"; return false; }
    std::string text(static_cast<size_t>(st.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(mi), text.data(), text.size(), 0)) { cleanup(); if (err) *err = "Manifesto ilegível"; return false; }
    json m = json::parse(text, nullptr, false);
    if (!m.is_object() || m.value("format", "") != "vsstage") { cleanup(); if (err) *err = "Arquivo não é um pacote VS Stage"; return false; }

    std::map<int64_t, int64_t> idMap;
    int count = 0;
    for (auto& sj : m.value("songs", json::array())) {
        SongEntry s = songFromJson(sj);
        const int64_t ref = sj.value("ref", int64_t(0));
        s.id = nextId();
        std::error_code ec;
        fs::create_directories(songDir(s.id), ec);
        std::vector<TrackEntry> kept;
        for (auto& t : s.tracks) {
            const std::string entry = "audio/" + std::to_string(ref) + "/" + t.file;
            int fi = mz_zip_reader_locate_file(&zip, entry.c_str(), nullptr, 0);
            if (fi < 0) continue;   // pacote sem áudio: a faixa é ignorada
            const std::string safe = safeFileName(t.file);
            std::ofstream f(songDir(s.id) / fs::u8path(safe), std::ios::binary | std::ios::trunc);
            bool ok = f && mz_zip_reader_extract_to_callback(&zip, static_cast<mz_uint>(fi), writeToStream, &f, 0);
            f.close();
            if (!ok) continue;
            t.file = safe;
            kept.push_back(t);
        }
        s.tracks = kept;
        idMap[ref] = s.id;
        songs.push_back(s);
        ++count;
    }
    if (m.contains("setlist") && m["setlist"].is_object()) {
        auto& o = m["setlist"];
        Setlist sl; sl.id = nextId(); sl.name = o.value("name", "Importado"); sl.dateLabel = o.value("dateLabel", ""); sl.notes = o.value("notes", "");
        for (auto& it : o.value("items", json::array())) {
            SetlistItem i = itemFromJson(it, "songRef");
            auto f = idMap.find(i.songId);
            if (f == idMap.end()) continue;
            i.songId = f->second;
            sl.items.push_back(i);
        }
        setlists.push_back(sl);
        if (outSetlist) *outSetlist = sl.id;
    }
    cleanup();
    if (outSongs) *outSongs = count;
    save();
    return true;
}

}  // namespace vss
