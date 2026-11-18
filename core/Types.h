#pragma once
// Tipos de domínio compartilhados (equivalentes a TrackRole/Section/SongMap do app Android).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/Constants.h"

namespace vss {

struct RoleInfo {
    const char* id;      // nome estável no JSON (igual ao do Android: "CLICK", "GUIDE"...)
    const char* label;   // rótulo em português
    vs::Bus bus;
    bool pitchLock;
    bool soloSafe;
    uint32_t colorRgb;   // 0xRRGGBB
};

enum class Role { Click, Guide, Drums, Perc, Bass, Acoustic, Electric, Keys, Pad, Strings, Backing, Vocal, LoopFx, Other, Count };

inline const RoleInfo& roleInfo(Role r) {
    static const RoleInfo t[] = {
        {"CLICK", "Click", vs::Bus::Click, true, true, 0xFFC400},
        {"GUIDE", "Guia", vs::Bus::Guide, true, true, 0xFF9100},
        {"DRUMS", "Bateria", vs::Bus::Band, true, false, 0xFF1744},
        {"PERC", "Percussão", vs::Bus::Band, true, false, 0xFF5252},
        {"BASS", "Baixo", vs::Bus::Band, false, false, 0x7C4DFF},
        {"ACOUSTIC", "Violão", vs::Bus::Band, false, false, 0x00E676},
        {"ELECTRIC", "Guitarra", vs::Bus::Band, false, false, 0x00B0FF},
        {"KEYS", "Teclado", vs::Bus::Band, false, false, 0x1DE9B6},
        {"PAD", "Pad", vs::Bus::Band, false, false, 0x64FFDA},
        {"STRINGS", "Cordas", vs::Bus::Band, false, false, 0xD500F9},
        {"BACKING", "Backing", vs::Bus::Band, false, false, 0xFFEA00},
        {"VOCAL", "Voz", vs::Bus::Band, false, false, 0xFFFFFF},
        {"LOOP_FX", "Loop/FX", vs::Bus::Band, true, false, 0x9E9E9E},
        {"OTHER", "Outro", vs::Bus::Band, false, false, 0xBDBDBD},
    };
    return t[static_cast<int>(r)];
}
inline Role roleFromId(const std::string& s) {
    for (int i = 0; i < static_cast<int>(Role::Count); ++i)
        if (s == roleInfo(static_cast<Role>(i)).id) return static_cast<Role>(i);
    return Role::Other;
}

struct Section {
    std::string name;
    uint32_t colorRgb = 0x00B0FF;
    int startBar = 0;  // 0-based
    int bars = 8;
    int endBar() const { return startBar + bars; }
};

/// Mapa da música: BPM original, compasso, 1º tempo e seções (conversões compasso <-> frame @48 kHz).
struct SongMap {
    double bpm = 120.0;
    int beatsPerBar = 4;
    double offsetMs = 0.0;
    std::vector<Section> sections;

    double offsetFrames() const { return offsetMs * vs::kEngineSampleRate / 1000.0; }
    double framesPerBeat() const { return 60.0 * vs::kEngineSampleRate / bpm; }
    double framesPerBar() const { return framesPerBeat() * beatsPerBar; }
    int64_t barStartFrame(int bar) const { return static_cast<int64_t>(std::llround(offsetFrames() + bar * framesPerBar())); }
    int64_t sectionStart(const Section& s) const { return barStartFrame(s.startBar); }
    int64_t sectionEnd(const Section& s) const { return barStartFrame(s.endBar()); }
    int barAt(int64_t frame) const { return static_cast<int>(std::floor((frame - offsetFrames()) / framesPerBar())); }
    int sectionIndexAt(int64_t frame) const {
        const int bar = barAt(frame);
        for (size_t i = 0; i < sections.size(); ++i)
            if (bar >= sections[i].startBar && bar < sections[i].endBar()) return static_cast<int>(i);
        return -1;
    }

    static const std::vector<std::string>& names() {
        static const std::vector<std::string> n = {"Intro", "Verso", "Pré-Refrão", "Refrão", "Ponte", "Espontâneo", "Instrumental", "Final"};
        return n;
    }
    static const std::vector<uint32_t>& colors() {
        static const std::vector<uint32_t> c = {0x00B0FF, 0x00E676, 0xFFEA00, 0xFF1744, 0xD500F9, 0xFF9100, 0x1DE9B6, 0x9E9E9E};
        return c;
    }

    /// Ordena por compasso inicial, remove inícios repetidos e recalcula durações (a última vai até o fim, se totalBars >= 0).
    static std::vector<Section> normalize(std::vector<Section> list, int totalBars) {
        std::stable_sort(list.begin(), list.end(), [](const Section& a, const Section& b) { return a.startBar < b.startBar; });
        std::vector<Section> out;
        for (auto& s : list) if (out.empty() || out.back().startBar != s.startBar) out.push_back(s);
        for (size_t i = 0; i < out.size(); ++i) {
            if (i + 1 < out.size()) out[i].bars = out[i + 1].startBar - out[i].startBar;
            else if (totalBars >= 0) out[i].bars = std::max(1, totalBars - out[i].startBar);
            else out[i].bars = std::max(1, out[i].bars);
        }
        return out;
    }

    /// Seções automáticas de 8 em 8 compassos (quando a música não tem seções salvas).
    static std::vector<Section> automatic(int64_t durationFrames, const SongMap& base, int barsPerSection = 8) {
        const int total = std::max(1, static_cast<int>(std::floor((durationFrames - base.offsetFrames()) / base.framesPerBar())));
        std::vector<Section> list;
        int bar = 0, i = 0;
        while (bar < total) {
            const int n = std::min(barsPerSection, total - bar);
            list.push_back({names()[i % names().size()], colors()[i % colors().size()], bar, n});
            bar += n; ++i;
        }
        return list;
    }
    int totalBars(int64_t durationFrames) const {
        return durationFrames > 0 ? std::max(1, static_cast<int>(std::floor((durationFrames - offsetFrames()) / framesPerBar()))) : -1;
    }
};

}  // namespace vss
