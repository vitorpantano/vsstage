// Teste de ponta a ponta sem placa de som: importa músicas sintéticas, carrega, toca (backend nulo), salta, exporta/importa pacote.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include "core/Library.h"
#include "core/Player.h"

using namespace vss;
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++fails; } } while (0)

static void writeWav(const fs::path& p, int seconds, double bpm, bool click, double freq) {
    const int sr = 48000, n = sr * seconds;
    std::ofstream f(p, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    f.write("RIFF", 4); w32(36 + n * 4); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
    f.write("data", 4); w32(n * 4);
    const double beat = 60.0 * sr / bpm;
    for (int i = 0; i < n; ++i) {
        double v;
        if (click) { double ph = std::fmod(i, beat); v = ph < 480 ? std::sin(ph * 0.5) * (1.0 - ph / 480.0) * 0.9 : 0.0; }
        else v = 0.3 * std::sin(2 * M_PI * freq * i / sr);
        int16_t s = static_cast<int16_t>(v * 32000);
        f.write(reinterpret_cast<char*>(&s), 2); f.write(reinterpret_cast<char*>(&s), 2);
    }
}

static bool waitLoaded(Player& p, int ms = 20000) {
    for (int i = 0; i < ms / 10; ++i) { p.update(); if (p.isLoaded()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return false;
}

int main() {
    #if defined(_WIN32)
    _putenv_s("VSS_AUDIO_BACKEND", "null");
#else
    setenv("VSS_AUDIO_BACKEND", "null", 1);
#endif
    fs::path tmp = fs::temp_directory_path() / "vss_core_test";
    fs::remove_all(tmp); fs::create_directories(tmp / "src");
    writeWav(tmp / "src" / "01 Teste Song - Click.wav", 30, 120.0, true, 0);
    writeWav(tmp / "src" / "01 Teste Song - Baixo.wav", 30, 120.0, false, 110);
    writeWav(tmp / "src" / "01 Teste Song - Teclado.wav", 30, 120.0, false, 440);

    Library lib(tmp / "home");
    lib.load();
    std::string err;
    int64_t id = lib.importSong({tmp / "src" / "01 Teste Song - Click.wav", tmp / "src" / "01 Teste Song - Baixo.wav", tmp / "src" / "01 Teste Song - Teclado.wav"}, "", &err);
    CHECK(id > 0, "importSong");
    SongEntry* s = lib.song(id);
    CHECK(s && s->title == "Teste Song", "título");
    CHECK(s && s->tracks.size() == 3, "3 faixas");
    CHECK(s && s->tracks[0].role == "CLICK", "papel do click");

    Player p;
    Settings st; st.channels = 2; st.periodFrames = 256;
    CHECK(p.startAudio(st), "abrir áudio (nulo)");
    bool loaded = false;
    p.loadSong(lib, *s, [&] { loaded = true; });
    CHECK(waitLoaded(p), "carregamento");
    CHECK(loaded, "callback de carga");
    CHECK(p.tracks.size() == 3 && p.tracks[0].slot >= 0, "faixas instaladas");
    std::printf("BPM detectado: %.2f  offset %.1f ms  seções %zu  duração %.1fs\n", p.map.bpm, p.map.offsetMs, p.map.sections.size(), p.duration() / 48000.0);
    CHECK(std::abs(p.map.bpm - 120.0) < 1.0, "detecção de BPM do click");
    CHECK(!p.map.sections.empty(), "seções automáticas");

    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    p.update();
    CHECK(p.playing(), "tocando");
    CHECK(p.position() > 48000 / 2, "posição avança");
    float maxPeak = 0; for (auto& t : p.tracks) maxPeak = std::max(maxPeak, t.peak);
    CHECK(maxPeak > 0.01f, "medidores");

    p.jumpToSection(1, vs::Quantize::Immediate);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    if (p.map.sections.size() > 1) CHECK(p.position() >= p.map.sectionStart(p.map.sections[1]) - 2000, "salto de seção");

    p.setTranspose(2); p.setTargetBpm(132);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    p.update();
    CHECK(p.engine().tempoRatio() > 1.05, "tempo ajustado");
    auto si = p.audio().info();
    std::printf("stream: %s %d Hz %d ch  CPU médio %.1f%% pico %.1f%%\n", si.backend.c_str(), si.sampleRate, si.channels, si.cpuAvg * 100, si.cpuPeak * 100);
    p.pause();
    p.setTranspose(0); p.setTargetBpm(0);

    // salva ajustes e seções
    p.markSectionHere();
    CHECK(p.mapDirty, "mapa sujo");
    s->sections = p.map.sections; s->bpm = p.map.bpm; s->offsetMs = p.map.offsetMs; s->analyzed = true;
    lib.save();

    // setlist + pacote
    int64_t slId = lib.newSetlist("Domingo");
    SetlistItem it; it.songId = id; it.transpose = 2; it.targetBpm = 126; it.muted.insert("Baixo"); it.gains["Teclado"] = 0.5f;
    lib.setlist(slId)->items.push_back(it);
    lib.save();
    CHECK(lib.exportPackage(tmp / "out.vsstage", slId, {}, true, &err), "exportar pacote");
    Library lib2(tmp / "home2"); lib2.load();
    int64_t newSl = -1; int nSongs = 0;
    CHECK(lib2.importPackage(tmp / "out.vsstage", &newSl, &nSongs, &err), "importar pacote");
    CHECK(nSongs == 1 && newSl > 0, "contagem do pacote");
    if (Setlist* sl = lib2.setlist(newSl)) {
        CHECK(sl->items.size() == 1 && sl->items[0].transpose == 2 && sl->items[0].targetBpm == 126, "ajustes do setlist");
        CHECK(sl->items[0].muted.count("Baixo") == 1 && sl->items[0].gains.count("Teclado") == 1, "mudos/ganhos");
        SongEntry* s2 = lib2.song(sl->items[0].songId);
        CHECK(s2 && s2->tracks.size() == 3 && s2->sections.size() == s->sections.size(), "música importada");
        CHECK(s2 && fs::exists(lib2.trackPath(*s2, s2->tracks[0])), "áudio extraído");
    } else CHECK(false, "setlist importado");

    // recarrega biblioteca do disco
    Library lib3(tmp / "home"); lib3.load();
    CHECK(lib3.songs.size() == 1 && lib3.setlists.size() == 1, "persistência JSON");

    // troca de música (clear durante carga)
    p.loadSong(lib, *s);
    p.loadSong(lib, *s);
    CHECK(waitLoaded(p), "recarga rápida");
    p.clear();

    std::printf(fails ? "FALHOU: %d\n" : "core OK\n", fails);
    fs::remove_all(tmp);
    return fails ? 1 : 0;
}
