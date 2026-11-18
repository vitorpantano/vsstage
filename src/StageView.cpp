#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include "App.h"
#include "Ui.h"
#include "imgui.h"

namespace vss {
using namespace ui;

static const char* kPadNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void App::drawStage(float w, float h) {
    const float leftW = std::clamp(w * 0.24f, 250.f * dpi_, 330.f * dpi_);
    ImGui::BeginChild("##left", ImVec2(leftW, h), ImGuiChildFlags_Borders);
    drawStageLeft(leftW, h);
    ImGui::EndChild();
    ImGui::SameLine();
    const float cw = w - leftW - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("##center", ImVec2(cw, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    drawSections(cw);
    drawSeekBar(cw);
    drawMixer(cw, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
}

// ------------------------------------------------------------------ painel de controles (esquerda)

void App::drawStageLeft(float w, float) {
    Player& p = player_;
    const float inner = ImGui::GetContentRegionAvail().x;
    if (p.songId < 0) {
        ImGui::Dummy(ImVec2(0, 20 * dpi_));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Nenhuma música carregada.");
        Caption("Vá em Biblioteca para importar os arquivos da música (ou gere a música de demonstração) e toque em \"Carregar no palco\".");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 10 * dpi_));
        if (ImGui::Button("Gerar música de demonstração", ImVec2(inner, 38 * dpi_))) seedDemo();
        return;
    }
    // título + setlist
    ImGui::PushTextWrapPos(inner);
    ImGui::PushStyleColor(ImGuiCol_Text, col(kText));
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(p.title.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    SongEntry* se = lib_.song(p.songId);
    if (se) {
        std::string info = se->artist.empty() ? "" : se->artist;
        if (!se->key.empty()) info += (info.empty() ? "" : " · ") + std::string("Tom ") + transposedKey(se->key, p.transposeSemis);
        if (!info.empty()) Caption(info.c_str());
    }
    if (curSetlist_ >= 0) {
        Setlist* sl = lib_.setlist(curSetlist_);
        if (sl) {
            char b[160];
            std::snprintf(b, sizeof b, "%s  ·  %d / %zu", sl->name.c_str(), curIndex_ + 1, sl->items.size());
            Caption(b);
            const float bw = (inner - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("◄ Anterior##song", ImVec2(bw, 0))) stepSetlist(-1);
            ImGui::SameLine();
            if (ImGui::Button("Próxima ►##song", ImVec2(bw, 0))) stepSetlist(+1);
            if (ImGui::Button("Salvar ajustes neste item", ImVec2(inner, 0))) { saveItemAdjustments(); toast("Ajustes salvos no setlist"); }
        }
    }
    if (p.pendingLoads() > 0) {
        ImGui::Dummy(ImVec2(0, 4));
        char b[64]; std::snprintf(b, sizeof b, "Carregando… faltam %d faixa(s)", p.pendingLoads());
        ImGui::PushStyleColor(ImGuiCol_Text, col(kWarn)); ImGui::TextUnformatted(b); ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // tempo / compasso
    char tb[96];
    std::snprintf(tb, sizeof tb, "%s / %s", formatTime(p.positionSec()).c_str(), formatTime(static_cast<double>(p.duration()) / vs::kEngineSampleRate).c_str());
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(tb);
    ImGui::SetWindowFontScale(1.f);
    const int bar = std::max(0, p.map.barAt(p.position())) + 1;
    const int sec = p.currentSection();
    std::snprintf(tb, sizeof tb, "Compasso %d%s%s", bar, sec >= 0 ? "  ·  " : "", sec >= 0 ? p.map.sections[static_cast<size_t>(sec)].name.c_str() : "");
    Caption(tb);

    // transporte
    const float bh = 54 * dpi_, gap = ImGui::GetStyle().ItemSpacing.x;
    const float sw = (inner - 3 * gap) / 4.f;
    if (IconButton("##prev", Icon::Prev, ImVec2(sw, bh))) p.jumpRelativeSection(-1);
    ImGui::SameLine();
    const bool playing = p.playing() || p.engine().isStarting();
    if (IconButton("##play", playing ? Icon::Pause : Icon::Play, ImVec2(sw, bh), playing, kGood)) p.togglePlay();
    ImGui::SameLine();
    if (IconButton("##stop", Icon::Stop, ImVec2(sw, bh))) p.stop();
    ImGui::SameLine();
    if (IconButton("##next", Icon::Next, ImVec2(sw, bh))) p.jumpRelativeSection(+1);
    if (IconButton("##loop", Icon::Loop, ImVec2(inner, 34 * dpi_), p.loopEnabled(), kWarn)) p.setLoopSection(std::max(0, sec), !p.loopEnabled());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loop da seção atual (L)");
    ImGui::Separator();

    // BPM
    Caption("ANDAMENTO (BPM)");
    const float sb = 40 * dpi_;
    if (IconButton("##bpm-", Icon::Minus, ImVec2(sb, sb))) p.stepBpm(ImGui::GetIO().KeyShift ? -0.1 : -1);
    ImGui::SameLine();
    {
        char b[32]; std::snprintf(b, sizeof b, "%.1f", p.currentBpm());
        ImGui::SetWindowFontScale(1.8f);
        const float tw = ImGui::CalcTextSize(b).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (inner - 2 * sb - 2 * gap - tw) * 0.5f));
        ImGui::AlignTextToFramePadding();
        const bool altered = p.targetBpm > 0;
        ImGui::PushStyleColor(ImGuiCol_Text, col(altered ? kWarn : kText)); ImGui::TextUnformatted(b); ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.f);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(inner - sb + ImGui::GetStyle().WindowPadding.x);
    if (IconButton("##bpm+", Icon::Plus, ImVec2(sb, sb))) p.stepBpm(ImGui::GetIO().KeyShift ? 0.1 : 1);
    {
        char b[64]; std::snprintf(b, sizeof b, "original: %.1f BPM", p.map.bpm);
        Caption(b);
        if (p.targetBpm > 0) { ImGui::SameLine(); if (ImGui::SmallButton("restaurar##bpm")) p.setTargetBpm(0); }
    }
    // TOM
    Caption("TOM (semitons)");
    if (IconButton("##tr-", Icon::Minus, ImVec2(sb, sb))) p.setTranspose(p.transposeSemis - 1);
    ImGui::SameLine();
    {
        char b[16]; std::snprintf(b, sizeof b, "%+d", p.transposeSemis);
        ImGui::SetWindowFontScale(1.8f);
        const float tw = ImGui::CalcTextSize(b).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (inner - 2 * sb - 2 * gap - tw) * 0.5f));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, col(p.transposeSemis != 0 ? kWarn : kText)); ImGui::TextUnformatted(b); ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.f);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(inner - sb + ImGui::GetStyle().WindowPadding.x);
    if (IconButton("##tr+", Icon::Plus, ImVec2(sb, sb))) p.setTranspose(p.transposeSemis + 1);
    if (p.transposeSemis != 0) { if (ImGui::SmallButton("restaurar##tr")) p.setTranspose(0); }
    ImGui::Separator();

    // PAD
    Caption("PAD AMBIENTE");
    const float kw = (inner - 5 * gap) / 6.f;
    for (int i = 0; i < 12; ++i) {
        if (i % 6) ImGui::SameLine();
        ImGui::PushID(i);
        if (ToggleButton(kPadNames[i], p.padKey == i, ImVec2(kw, 34 * dpi_), 0x64FFDA)) p.padSelect(p.padKey == i ? -1 : i);
        ImGui::PopID();
    }
    float pg = p.padGain;
    ImGui::SetNextItemWidth(inner);
    if (ImGui::SliderFloat("##padgain", &pg, 0.f, 1.f, "Volume do pad %.2f")) p.setPadGain(pg);
    ImGui::Separator();
    if (ImGui::Button("Editar seções…", ImVec2(inner, 0))) {
        editSections_ = p.map.sections;
        openSectionEditor_ = true;
    }
    (void)w;
}

// ------------------------------------------------------------------ faixa de seções

void App::drawSections(float w) {
    Player& p = player_;
    const float h = 58 * dpi_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gap = 6 * dpi_;
    const auto& secs = p.map.sections;
    ImGui::BeginChild("##sections", ImVec2(w, h + 8), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    if (secs.empty()) { Caption("Sem seções (carregue uma música)"); ImGui::EndChild(); return; }
    int total = 0; for (auto& s : secs) total += s.bars;
    const float avail = w - gap * static_cast<float>(secs.size());
    const int cur = p.currentSection(), pend = p.pendingSection();
    for (size_t i = 0; i < secs.size(); ++i) {
        const float sw = std::max(86.f * dpi_, avail * static_cast<float>(secs[i].bars) / static_cast<float>(std::max(1, total)));
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##sec", ImVec2(sw, h))) p.jumpToSection(static_cast<int>(i));
        const bool hov = ImGui::IsItemHovered();
        const bool isCur = static_cast<int>(i) == cur, isPend = static_cast<int>(i) == pend;
        const uint32_t c = secs[i].colorRgb;
        dl->AddRectFilled(pos, ImVec2(pos.x + sw, pos.y + h), col32(c, isCur ? 0.95f : (hov ? 0.45f : 0.28f)), 8);
        if (isPend) dl->AddRect(pos, ImVec2(pos.x + sw, pos.y + h), col32(0xFFFFFF), 8, 0, 3.f);
        else if (isCur) dl->AddRect(pos, ImVec2(pos.x + sw, pos.y + h), col32(0xFFFFFF, 0.6f), 8, 0, 1.5f);
        const uint32_t tc = isCur ? 0x000000 : kText;
        dl->AddText(ImVec2(pos.x + 10 * dpi_, pos.y + 8 * dpi_), col32(tc), secs[i].name.c_str());
        char b[48]; std::snprintf(b, sizeof b, "compasso %d · %d comp.", secs[i].startBar + 1, secs[i].bars);
        dl->AddText(ImVec2(pos.x + 10 * dpi_, pos.y + 8 * dpi_ + ImGui::GetFontSize() + 3), col32(tc, isCur ? 0.75f : 0.6f), b);
        if (p.loopEnabled() && p.loopSectionIndex() == static_cast<int>(i))
            dl->AddText(ImVec2(pos.x + sw - 22 * dpi_, pos.y + 8 * dpi_), col32(isCur ? 0x000000 : kWarn), "L");
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::drawSeekBar(float w) {
    Player& p = player_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = 20 * dpi_;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##seek", ImVec2(w, h));
    const int64_t dur = std::max<int64_t>(1, p.duration());
    if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
        if (ImGui::IsMouseClicked(0) || ImGui::IsMouseReleased(0)) {}
    }
    if (ImGui::IsItemClicked()) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - a.x) / w, 0.f, 1.f);
        const int64_t target = static_cast<int64_t>(t * static_cast<float>(dur));
        if (p.playing()) p.engine().jumpTo(target, vs::Quantize::NextBeat); else p.seekFrame(target);
    }
    dl->AddRectFilled(ImVec2(a.x, a.y + h * 0.35f), ImVec2(a.x + w, a.y + h * 0.65f), col32(kPanel2), 4);
    for (auto& s : p.map.sections) {
        const float x0 = a.x + w * static_cast<float>(p.map.sectionStart(s)) / static_cast<float>(dur);
        const float x1 = a.x + w * std::min(1.f, static_cast<float>(p.map.sectionEnd(s)) / static_cast<float>(dur));
        if (x1 > x0) dl->AddRectFilled(ImVec2(x0 + 1, a.y + h * 0.35f), ImVec2(x1 - 1, a.y + h * 0.65f), col32(s.colorRgb, 0.55f), 3);
    }
    const float px = a.x + w * std::clamp(static_cast<float>(p.position()) / static_cast<float>(dur), 0.f, 1.f);
    dl->AddRectFilled(ImVec2(px - 1.5f, a.y + 1), ImVec2(px + 1.5f, a.y + h - 1), col32(0xFFFFFF), 1);
    ImGui::Dummy(ImVec2(0, 2));
}

// ------------------------------------------------------------------ mixer (todas as faixas visíveis, faders verticais)

void App::drawMixer(float w, float h) {
    Player& p = player_;
    const float masterW = 168 * dpi_;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float stripsW = w - masterW - gap;
    const size_t n = p.tracks.size();
    const float minW = 74 * dpi_, maxW = 170 * dpi_;
    float sw = n ? (stripsW - gap * static_cast<float>(n - 1)) / static_cast<float>(n) : maxW;
    sw = std::clamp(sw, minW, maxW);

    ImGui::BeginChild("##strips", ImVec2(stripsW, h), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    const float sh = ImGui::GetContentRegionAvail().y - (ImGui::GetScrollMaxX() > 0 ? 14 * dpi_ : 0);
    if (n == 0) { ImGui::Dummy(ImVec2(0, 10)); Caption("Sem faixas."); }
    for (size_t i = 0; i < n; ++i) {
        PlayerTrack& t = p.tracks[i];
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(static_cast<int>(i));
        const uint32_t rc = roleInfo(t.role).colorRgb;
        ImGui::BeginChild("##strip", ImVec2(sw, sh - 6), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddRectFilled(ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + sw - 1, wp.y + 5 * dpi_), col32(rc), 6, ImDrawFlags_RoundCornersTop);
        const float in = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0, 2 * dpi_));
        TextCentered(t.name, in, t.failed ? kBad : kText);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
        {
            const std::string rl = roleInfo(t.role).label;
            auto low = [](std::string x) { for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return x; };
            const bool same = low(rl) == low(t.name);
            TextCentered(t.loading ? "carregando…" : (t.failed ? "erro" : (same ? "" : rl)), in, kDim);
        }

        const float btnH = 30 * dpi_;
        const float footer = btnH * 3 + ImGui::GetStyle().ItemSpacing.y * 3 + ImGui::GetTextLineHeight() + 6;
        const float fh = std::max(60.f * dpi_, ImGui::GetContentRegionAvail().y - footer);
        const float meterW = 14 * dpi_, faderW = std::min(44.f * dpi_, in - meterW - 24 * dpi_);
        const float used = meterW + faderW + 6 * dpi_;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (in - used) * 0.5f));
        ImGui::BeginGroup();
        VuMeter(ImVec2(meterW, fh), t.peak);
        ImGui::SameLine(0, 4 * dpi_);
        float g = t.gain;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, col(0x0A0A0D));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, col(t.mute ? kDim : 0xE8E8EE));
        if (ImGui::VSliderFloat("##gain", ImVec2(faderW, fh), &g, 0.f, vs::kMaxTrackGain, "")) p.setGain(i, g);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) p.setGain(i, 1.0f);   // duplo clique = 0 dB
        ImGui::EndGroup();
        {
            char b[24];
            const float db = toDb(t.gain);
            if (db < -60.f) std::snprintf(b, sizeof b, "-∞ dB"); else std::snprintf(b, sizeof b, "%+.1f dB", db);
            TextCentered(b, in, kDim);
        }
        const float bw = in;
        if (ToggleButton("M", t.mute, ImVec2(bw, btnH), kBad, 0xFFFFFF)) p.setMute(i, !t.mute);
        if (ToggleButton("S", t.solo, ImVec2(bw, btnH), kWarn)) p.setSolo(i, !t.solo);
        const char* busLabel = t.bus == vs::Bus::Band ? "BANDA" : (t.bus == vs::Bus::Click ? "CLICK" : "GUIA");
        if (ImGui::Button(busLabel, ImVec2(bw, btnH))) {
            const vs::Bus next = t.bus == vs::Bus::Band ? vs::Bus::Click : (t.bus == vs::Bus::Click ? vs::Bus::Guide : vs::Bus::Band);
            p.setBus(i, next);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Barramento de saída (Banda / Click / Guia)");
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();

    // ---- master ----
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##master", ImVec2(masterW, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    const float in = ImGui::GetContentRegionAvail().x;
    TextCentered("MASTER", in, kAccent);
    const float eqH = 90 * dpi_;
    const float fh = std::max(60.f * dpi_, ImGui::GetContentRegionAvail().y - eqH - 70 * dpi_);
    {
        float g = p.masterGain;
        const float fw = 40 * dpi_;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (in - fw - 16 * dpi_) * 0.5f);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, col(0x0A0A0D));
        if (ImGui::VSliderFloat("##master", ImVec2(fw, fh), &g, 0.f, 1.5f, "")) { p.setMasterGain(g); lib_.settings.masterGain = g; libDirty_ = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4 * dpi_);
        // barra de redução do limiter
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(10 * dpi_, fh));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + 10 * dpi_, a.y + fh), col32(0x08080A), 3);
        const float red = std::clamp(-p.limiterReductionDb / 12.f, 0.f, 1.f);
        dl->AddRectFilled(a, ImVec2(a.x + 10 * dpi_, a.y + fh * red), col32(kBad), 3);
        ImGui::EndGroup();
        char b[24]; const float db = toDb(p.masterGain);
        std::snprintf(b, sizeof b, "%+.1f dB", db);
        TextCentered(b, in, kDim);
        TextCentered(p.limiterReductionDb < -0.1f ? "limiter ativo" : "limiter", in, p.limiterReductionDb < -0.1f ? kBad : kDim);
    }
    // EQ de 3 bandas
    {
        const char* names[3] = {"BX", "MD", "AG"};
        float* vals[3] = {&lib_.settings.eqLow, &lib_.settings.eqMid, &lib_.settings.eqHigh};
        const float cw = in / 3.f;
        bool changed = false;
        ImGui::BeginGroup();
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, 0);
            ImGui::BeginGroup();
            ImGui::PushID(i);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - 24 * dpi_) * 0.5f);
            float v = *vals[i];
            if (ImGui::VSliderFloat("##eq", ImVec2(24 * dpi_, eqH - 24 * dpi_), &v, -12.f, 12.f, "")) { *vals[i] = v; changed = true; }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { *vals[i] = 0; changed = true; }
            ImGui::PopID();
            TextCentered(names[i], cw, kDim);
            ImGui::EndGroup();
        }
        ImGui::EndGroup();
        if (changed) { p.engine().setEq(lib_.settings.eqLow, lib_.settings.eqMid, lib_.settings.eqHigh); libDirty_ = true; }
    }
    ImGui::EndChild();
}

}  // namespace vss
