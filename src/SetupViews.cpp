#include <algorithm>
#include <cstdio>

#include "App.h"
#include "Ui.h"
#include "imgui.h"

namespace vss {
using namespace ui;

static const char* kSrcNames[] = {"Mudo", "Banda L", "Banda R", "Banda (mono)", "Click", "Guia", "Click + Guia", "Tudo L", "Tudo R"};

void App::drawOutput(float w, float h) {
    if (!devicesLoaded_) refreshDevices();
    if (!settingsInit_) { editSettings_ = lib_.settings; settingsInit_ = true; }
    Settings& e = editSettings_;
    ImGui::BeginChild("##out", ImVec2(w, h), ImGuiChildFlags_Borders);
    const float in = std::min(ImGui::GetContentRegionAvail().x, 760.f * dpi_);
    Caption("DISPOSITIVO DE SAÍDA");
    int maxCh = 2;
    {
        const std::string cur = e.deviceName.empty() ? "Padrão do sistema" : e.deviceName;
        ImGui::SetNextItemWidth(in);
        if (ImGui::BeginCombo("##dev", cur.c_str())) {
            if (ImGui::Selectable("Padrão do sistema", e.deviceName.empty())) e.deviceName.clear();
            for (auto& d : devices_) {
                char b[256]; std::snprintf(b, sizeof b, "%s  (até %d canais)", d.name.c_str(), d.maxChannels);
                if (ImGui::Selectable(b, d.name == e.deviceName)) e.deviceName = d.name;
            }
            ImGui::EndCombo();
        }
        for (auto& d : devices_) if (e.deviceName.empty() ? d.isDefault : d.name == e.deviceName) maxCh = d.maxChannels;
    }
    if (ImGui::Button("Atualizar lista")) refreshDevices();
    ImGui::SameLine(); Caption("Interfaces USB class-compliant aparecem aqui. Para ASIO, use um driver ASIO→WASAPI (ex.: FlexASIO) ou o driver WASAPI do fabricante.");

    ImGui::Dummy(ImVec2(0, 4));
    Caption("CANAIS DE SAÍDA");
    static const int chOpts[] = {2, 4, 6, 8, 10, 12, 16};
    bool firstCh = true;
    for (int c : chOpts) {
        if (c > std::max(maxCh, 2) && c != e.channels) continue;
        char b[16]; std::snprintf(b, sizeof b, "%d", c);
        if (!firstCh) ImGui::SameLine(0, 6);
        firstCh = false;
        if (ToggleButton(b, e.channels == c, ImVec2(46 * dpi_, 0), kAccent)) e.channels = c;
    }
    ImGui::Dummy(ImVec2(0, 4));
    Caption("BUFFER (quadros por período — menor = menos latência, maior = mais estável)");
    static const int bufs[] = {64, 128, 192, 256, 384, 512, 1024};
    for (int b : bufs) {
        char s[16]; std::snprintf(s, sizeof s, "%d", b);
        if (b != bufs[0]) ImGui::SameLine(0, 6);
        if (ToggleButton(s, e.periodFrames == b, ImVec2(52 * dpi_, 0), kAccent)) e.periodFrames = b;
    }
    ImGui::Checkbox("Modo exclusivo (WASAPI) — menor latência, bloqueia outros apps", &e.exclusive);
    ImGui::Separator();

    if (e.channels <= 2) {
        Caption("ROTEAMENTO ESTÉREO");
        if (ImGui::RadioButton("Split: Click + Guia à ESQUERDA, Banda à DIREITA (cabo P2 / fone de ouvido)", e.routing == 1)) e.routing = 1;
        if (ImGui::RadioButton("Estéreo: Banda em L/R, Click + Guia centralizados", e.routing == 0)) e.routing = 0;
    } else {
        Caption("MATRIZ DE SAÍDAS (o que sai em cada canal da interface)");
        if (e.channelMap.size() != static_cast<size_t>(e.channels)) {
            std::vector<int> m(static_cast<size_t>(e.channels), 0);
            const int def[] = {1, 2, 4, 5};   // BandL, BandR, Click, Guide
            for (size_t i = 0; i < m.size() && i < 4; ++i) m[i] = def[i];
            for (size_t i = 0; i < std::min(m.size(), e.channelMap.size()); ++i) m[i] = e.channelMap[i];
            e.channelMap = m;
        }
        if (ImGui::BeginTable("##map", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(in, 0))) {
            for (int c = 0; c < e.channels; ++c) {
                ImGui::PushID(c);
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("Saída %d", c + 1);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
                int& v = e.channelMap[static_cast<size_t>(c)];
                if (ImGui::BeginCombo("##src", kSrcNames[std::clamp(v, 0, 8)])) {
                    for (int k = 0; k < 9; ++k) if (ImGui::Selectable(kSrcNames[k], v == k)) v = k;
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button("Aplicar e reiniciar a saída", ImVec2(260 * dpi_, 40 * dpi_))) {
        if (e.channels <= 2) e.channelMap.clear();
        restartAudio();
    }
    ImGui::Separator();
    Caption("STATUS");
    StreamInfo si = player_.audio().info();
    if (si.open) {
        ImGui::Text("Backend: %s%s  ·  %d Hz  ·  %d canais  ·  período %d quadros", si.backend.c_str(), si.exclusive ? " (exclusivo)" : "", si.sampleRate, si.channels, si.periodFrames);
        ImGui::Text("Latência estimada de saída: %.1f ms  ·  CPU do áudio: %.0f%% (pico %.0f%%)  ·  estouros de tempo: %ld", si.latencyMs, si.cpuAvg * 100.f, si.cpuPeak * 100.f, si.overruns);
        Caption("Com tom ou BPM alterados o motor usa mais CPU; se houver estalos, aumente o buffer.");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, col(kBad)); ImGui::TextUnformatted("Saída de áudio fechada."); ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void App::drawPedals(float w, float h) {
    ImGui::BeginChild("##ped", ImVec2(w, h), ImGuiChildFlags_Borders);
    const float in = std::min(ImGui::GetContentRegionAvail().x, 760.f * dpi_);
    Caption("ENTRADA MIDI (pedaleira / controlador USB)");
    const std::string cur = midi_.isOpen() ? midi_.openName() : "Nenhuma";
    ImGui::SetNextItemWidth(in);
    if (ImGui::BeginCombo("##midiin", cur.c_str())) {
        if (ImGui::Selectable("Nenhuma", !midi_.isOpen())) { midi_.open(""); lib_.settings.midiInput.clear(); lib_.save(); }
        for (auto& pn : midiPorts_) if (ImGui::Selectable(pn.c_str(), pn == midi_.openName())) {
            if (midi_.open(pn)) { lib_.settings.midiInput = pn; lib_.save(); } else toast("Não foi possível abrir " + pn);
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Atualizar portas")) midiPorts_ = midi_.ports();
    ImGui::SameLine();
    Caption(lastMidiLabel_.empty() ? "Aperte um botão do pedal para testar." : ("Última mensagem: " + lastMidiLabel_).c_str());
    ImGui::Separator();
    Caption("AÇÕES  (clique em \"Aprender\" e aperte o botão do pedal)");
    if (ImGui::BeginTable("##acts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(in, 0))) {
        ImGui::TableSetupColumn("Ação", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Botão", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 190 * dpi_);
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            const Action a = static_cast<Action>(i);
            ImGui::PushID(i);
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(actionLabel(a));
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            const MidiBinding* bound = nullptr;
            for (auto& m : lib_.settings.midi) if (m.action == actionId(a)) bound = &m;
            if (learning_ && learnAction_ == a) { ImGui::PushStyleColor(ImGuiCol_Text, col(kWarn)); ImGui::TextUnformatted("Aperte o botão do pedal…"); ImGui::PopStyleColor(); }
            else if (bound) {
                char b[64]; std::snprintf(b, sizeof b, "%s %d (canal %d)", bound->type == 0 ? "Nota" : (bound->type == 1 ? "CC" : "Program"), bound->number, bound->channel);
                ImGui::TextUnformatted(b);
            } else Caption("—");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(learning_ && learnAction_ == a ? "Cancelar" : "Aprender")) { if (learning_ && learnAction_ == a) learning_ = false; else { learning_ = true; learnAction_ = a; } }
            if (bound) { ImGui::SameLine(); if (ImGui::SmallButton("Limpar")) {
                auto& v = lib_.settings.midi;
                v.erase(std::remove_if(v.begin(), v.end(), [&](const MidiBinding& x) { return x.action == actionId(a); }), v.end());
                lib_.save();
            } }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6));
    Caption("Atalhos do teclado no Palco: Espaço = play/pause · ←/→ = seção · ↑/↓ = BPM (Shift = tom) · L = loop · Esc = stop · PgUp/PgDn = música.");
    ImGui::EndChild();
}

}  // namespace vss
