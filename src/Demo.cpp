#include "Demo.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>

namespace vss {
namespace fs = std::filesystem;

static void writeWav(const fs::path& p, const std::vector<float>& mono) {
    std::ofstream f(p, std::ios::binary);
    const uint32_t n = static_cast<uint32_t>(mono.size()), sr = 48000;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); w32(36 + n * 4); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
    f.write("data", 4); w32(n * 4);
    for (float v : mono) {
        const int16_t s = static_cast<int16_t>(std::max(-1.f, std::min(1.f, v)) * 30000);
        f.write(reinterpret_cast<const char*>(&s), 2); f.write(reinterpret_cast<const char*>(&s), 2);
    }
}

std::vector<fs::path> generateDemoSong(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const double bpm = 96.0, sr = 48000.0, beat = 60.0 * sr / bpm;
    const int bars = 32, total = static_cast<int>(beat * 4 * bars);
    std::vector<float> click(total), drums(total), bass(total), keys(total), pad(total);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> nz(-1.f, 1.f);
    // progressão: C  G  Am  F (um compasso cada)
    const double roots[4] = {130.81, 98.0, 110.0, 87.31};
    const double thirds[4] = {1.2599, 1.2599, 1.1892, 1.2599};
    const double fifths = 1.4983;
    for (int i = 0; i < total; ++i) {
        const double inBeat = std::fmod(i, beat);
        const int beatIdx = static_cast<int>(i / beat), bar = beatIdx / 4, b = beatIdx % 4, ch = bar % 4;
        // click: 1º tempo mais agudo
        if (inBeat < 600) click[i] = static_cast<float>(std::sin(inBeat * (b == 0 ? 0.22 : 0.15)) * (1.0 - inBeat / 600.0) * 0.8);
        // bateria: bumbo 1 e 3, caixa 2 e 4, chimbal em colcheias
        float d = 0;
        if ((b == 0 || b == 2) && inBeat < 9000) d += static_cast<float>(std::sin(2 * M_PI * (60 + 90 * std::exp(-inBeat / 1500.0)) * inBeat / sr) * std::exp(-inBeat / 3000.0) * 0.9);
        if ((b == 1 || b == 3) && inBeat < 8000) d += static_cast<float>(nz(rng) * std::exp(-inBeat / 1800.0) * 0.5);
        const double half = std::fmod(i, beat / 2);
        if (half < 1500) d += static_cast<float>(nz(rng) * std::exp(-half / 250.0) * 0.18);
        drums[i] = d;
        // baixo: raiz em semínimas com decaimento
        bass[i] = static_cast<float>(std::sin(2 * M_PI * roots[ch] * 0.5 * i / sr) * std::exp(-inBeat / 22000.0) * 0.6);
        // teclado: acorde em mínimas
        const double inHalf = std::fmod(i, beat * 2);
        const double env = std::exp(-inHalf / 40000.0) * 0.18;
        const double f0 = roots[ch] * 2;
        keys[i] = static_cast<float>(env * (std::sin(2 * M_PI * f0 * i / sr) + std::sin(2 * M_PI * f0 * thirds[ch] * i / sr) + std::sin(2 * M_PI * f0 * fifths * i / sr)));
        // pad: acorde sustentado
        const double pe = 0.1 * (0.6 + 0.4 * std::sin(2 * M_PI * i / (beat * 8)));
        pad[i] = static_cast<float>(pe * (std::sin(2 * M_PI * roots[ch] * i / sr) + std::sin(2 * M_PI * roots[ch] * thirds[ch] * i / sr) * 0.7 + std::sin(2 * M_PI * roots[ch] * fifths * i / sr) * 0.7));
    }
    std::vector<fs::path> out = {dir / "01 Demo Louvor - Click.wav", dir / "01 Demo Louvor - Bateria.wav", dir / "01 Demo Louvor - Baixo.wav",
                                 dir / "01 Demo Louvor - Teclado.wav", dir / "01 Demo Louvor - Pad.wav"};
    writeWav(out[0], click); writeWav(out[1], drums); writeWav(out[2], bass); writeWav(out[3], keys); writeWav(out[4], pad);
    return out;
}

}  // namespace vss
