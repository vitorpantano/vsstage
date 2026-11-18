#include <algorithm>
#include <cstdio>
#include <cstring>

#include "App.h"
#include "FileDialog.h"
#include "Ui.h"
#include "imgui.h"

namespace vss {
using namespace ui;

static std::string u8s(const fs::path& p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }

static bool inputText(const char* label, std::string& s, float width = -1) {
    char buf[256];
    std::strncpy(buf, s.c_str(), sizeof buf - 1); buf[sizeof buf - 1] = 0;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buf, sizeof buf)) { s = buf; return true; }
    return false;
}

// ------------------------------------------------------------------ Biblioteca

void App::drawLibrary(float w, float h) {
    const float listW = std::clamp(w * 0.38f, 320.f * dpi_, 520.f * dpi_);
    ImGui::BeginChild("##songs", ImVec2(listW, h), ImGuiChildFlags_Borders);
    const float inner = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::Button("+ Importar arquivos", ImVec2((inner - gap) * 0.5f, 36 * dpi_))) openImport(dlg::openAudio(hwnd_));
    ImGui::SameLine();
    if (ImGui::Button("+ Importar pasta", ImVec2((inner - gap) * 0.5f, 36 * dpi_))) {
        fs::path d = dlg::pickFolder(hwnd_);
        if (!d.empty()) openImport(dlg::audioFilesIn(d));
    }
    if (ImGui::Button("Importar pacote .vsstage", ImVec2(inner, 0))) {
        fs::path pk = dlg::openPackage(hwnd_);
        if (!pk.empty()) {
            std::string err; int64_t sl = -1; int n = 0;
            if (lib_.importPackage(pk, &sl, &n, &err)) { toast("Pacote importado: " + std::to_string(n) + " música(s)"); if (sl >= 0) selSetlist_ = sl; }
            else toast(err);
        }
    }
    Caption("Dica: arraste arquivos ou uma pasta para esta janela.");
    ImGui::Separator();
    if (lib_.songs.empty()) {
        ImGui::PushTextWrapPos(0);
        Caption("Sua biblioteca está vazia. Importe os arquivos de uma música (Click, Guia, Bateria, Baixo…) — o app reconhece o papel de cada faixa pelo nome.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Gerar música de demonstração", ImVec2(inner, 36 * dpi_))) seedDemo();
    }
    ImGui::BeginChild("##songlist", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    std::vector<size_t> order(lib_.songs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return lib_.songs[a].title < lib_.songs[b].title; });
    for (size_t k : order) {
        SongEntry& s = lib_.songs[k];
        ImGui::PushID(static_cast<int>(s.id));
        char b[256];
        std::snprintf(b, sizeof b, "%s\n%.1f BPM%s%s · %zu faixas", s.title.c_str(), s.bpm, s.key.empty() ? "" : " · Tom ", s.key.c_str(), s.tracks.size());
        if (ImGui::Selectable(b, selSong_ == s.id, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, ImGui::GetTextLineHeight() * 2 + 8))) {
            selSong_ = s.id;
            if (ImGui::IsMouseDoubleClicked(0)) { loadSong(s.id); tab_ = Tab::Stage; }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##detail", ImVec2(0, h), ImGuiChildFlags_Borders);
    SongEntry* s = lib_.song(selSong_);
    if (!s) { ImGui::Dummy(ImVec2(0, 20)); Caption("Selecione uma música à esquerda."); ImGui::EndChild(); return; }
    const float in = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(static_cast<int>(s->id));
    Caption("TÍTULO");
    if (inputText("##title", s->title, in)) libDirty_ = true;
    const float half = (in - gap) * 0.5f;
    Caption("ARTISTA / TOM");
    if (inputText("##artist", s->artist, half)) libDirty_ = true;
    ImGui::SameLine();
    if (inputText("##key", s->key, half)) libDirty_ = true;
    ImGui::SetNextItemWidth(half);
    float bpm = static_cast<float>(s->bpm);
    if (ImGui::DragFloat("##bpm", &bpm, 0.1f, 40.f, 240.f, "%.1f BPM")) { s->bpm = bpm; libDirty_ = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    int bpb = s->beatsPerBar;
    if (ImGui::DragInt("##bpb", &bpb, 0.1f, 2, 12, "%d tempos por compasso")) { s->beatsPerBar = bpb; libDirty_ = true; }
    ImGui::SetNextItemWidth(in);
    float off = static_cast<float>(s->offsetMs);
    if (ImGui::DragFloat("##off", &off, 1.f, 0.f, 20000.f, "1º tempo em %.0f ms")) { s->offsetMs = off; libDirty_ = true; }
    ImGui::Separator();
    Caption("FAIXAS");
    if (ImGui::BeginTable("##tracks", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Papel", ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableSetupColumn("Arquivo", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.f * dpi_);
        int removeIdx = -1;
        for (size_t i = 0; i < s->tracks.size(); ++i) {
            TrackEntry& t = s->tracks[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (inputText("##n", t.name)) libDirty_ = true;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            const Role cur = roleFromId(t.role);
            if (ImGui::BeginCombo("##role", roleInfo(cur).label)) {
                for (int r = 0; r < static_cast<int>(Role::Count); ++r)
                    if (ImGui::Selectable(roleInfo(static_cast<Role>(r)).label, r == static_cast<int>(cur))) { t.role = roleInfo(static_cast<Role>(r)).id; libDirty_ = true; }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            Caption(t.file.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Button("Remover")) removeIdx = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removeIdx >= 0) {
            if (player_.songId == s->id) player_.clear();
            lib_.removeTrack(s->id, static_cast<size_t>(removeIdx));
        }
    }
    if (ImGui::Button("+ Adicionar faixas")) { auto f = dlg::openAudio(hwnd_); if (!f.empty()) openImport(f, s->id); }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    const float bw = (in - 2 * gap) / 3.f;
    if (ImGui::Button("▶ Carregar no palco", ImVec2(bw, 40 * dpi_))) { loadSong(s->id); tab_ = Tab::Stage; }
    ImGui::SameLine();
    if (ImGui::Button("Exportar pacote…", ImVec2(bw, 40 * dpi_))) {
        fs::path out = dlg::savePackage(hwnd_, s->title + ".vsstage");
        if (!out.empty()) { std::string err; if (lib_.exportPackage(out, -1, {s->id}, true, &err)) toast("Pacote salvo em " + u8s(out)); else toast(err); }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, col(0x4A1620));
    if (ImGui::Button("Excluir música", ImVec2(bw, 40 * dpi_))) confirmDeleteSong_ = true;
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::EndChild();
}

// ------------------------------------------------------------------ Setlists

void App::drawSetlists(float w, float h) {
    const float listW = std::clamp(w * 0.28f, 260.f * dpi_, 400.f * dpi_);
    ImGui::BeginChild("##sls", ImVec2(listW, h), ImGuiChildFlags_Borders);
    const float inner = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::Button("+ Novo setlist", ImVec2(inner, 36 * dpi_))) selSetlist_ = lib_.newSetlist("Setlist " + std::to_string(lib_.setlists.size() + 1));
    if (ImGui::Button("Importar pacote…", ImVec2(inner, 0))) {
        fs::path pk = dlg::openPackage(hwnd_);
        if (!pk.empty()) {
            std::string err; int64_t sl = -1; int n = 0;
            if (lib_.importPackage(pk, &sl, &n, &err)) { toast("Pacote importado"); if (sl >= 0) selSetlist_ = sl; } else toast(err);
        }
    }
    ImGui::Separator();
    for (auto& sl : lib_.setlists) {
        ImGui::PushID(static_cast<int>(sl.id));
        char b[256]; std::snprintf(b, sizeof b, "%s\n%zu música(s)%s%s", sl.name.c_str(), sl.items.size(), sl.dateLabel.empty() ? "" : " · ", sl.dateLabel.c_str());
        if (ImGui::Selectable(b, selSetlist_ == sl.id, 0, ImVec2(0, ImGui::GetTextLineHeight() * 2 + 8))) selSetlist_ = sl.id;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##slx", ImVec2(0, h), ImGuiChildFlags_Borders);
    Setlist* sl = lib_.setlist(selSetlist_);
    if (!sl) { Caption("Crie ou selecione um setlist."); ImGui::EndChild(); return; }
    const float in = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(static_cast<int>(sl->id));
    if (inputText("##slname", sl->name, in * 0.55f)) libDirty_ = true;
    ImGui::SameLine();
    if (inputText("##sldate", sl->dateLabel, in * 0.25f)) libDirty_ = true;
    ImGui::SameLine();
    Caption("nome  ·  data");
    ImGui::Separator();
    if (ImGui::BeginTable("##items", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0, h - 200 * dpi_))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30 * dpi_);
        ImGui::TableSetupColumn("Música", ImGuiTableColumnFlags_WidthStretch, 4.f);
        ImGui::TableSetupColumn("Tom", ImGuiTableColumnFlags_WidthFixed, 130 * dpi_);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 100 * dpi_);
        ImGui::TableSetupColumn("Pad", ImGuiTableColumnFlags_WidthFixed, 80 * dpi_);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 200 * dpi_);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        int rm = -1, mv = 0, mvIdx = -1, play = -1;
        for (size_t i = 0; i < sl->items.size(); ++i) {
            SetlistItem& it = sl->items[i];
            SongEntry* s = lib_.song(it.songId);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            const bool isCur = curSetlist_ == sl->id && curIndex_ == static_cast<int>(i);
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col(isCur ? kGood : kText));
            ImGui::TextUnformatted(s ? s->title.c_str() : "(música removida)");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("-")) { it.transpose = std::max(-6, it.transpose - 1); libDirty_ = true; }
            ImGui::SameLine(); ImGui::Text("%+d", it.transpose); ImGui::SameLine();
            if (ImGui::SmallButton("+")) { it.transpose = std::min(6, it.transpose + 1); libDirty_ = true; }
            if (s && !s->key.empty()) { ImGui::SameLine(); Caption(transposedKey(s->key, it.transpose).c_str()); }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            float tb = static_cast<float>(it.targetBpm);
            if (ImGui::DragFloat("##tb", &tb, 0.2f, 0.f, 240.f, tb > 0 ? "%.1f" : "original")) { it.targetBpm = tb < 30.f ? 0.0 : tb; libDirty_ = true; }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            static const char* pk[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            if (ImGui::BeginCombo("##pad", it.padKey < 0 ? "—" : pk[it.padKey % 12])) {
                if (ImGui::Selectable("—", it.padKey < 0)) { it.padKey = -1; libDirty_ = true; }
                for (int k = 0; k < 12; ++k) if (ImGui::Selectable(pk[k], it.padKey == k)) { it.padKey = k; libDirty_ = true; }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("▶")) play = static_cast<int>(i);
            ImGui::SameLine(); if (ImGui::SmallButton("↑")) { mv = -1; mvIdx = static_cast<int>(i); }
            ImGui::SameLine(); if (ImGui::SmallButton("↓")) { mv = 1; mvIdx = static_cast<int>(i); }
            ImGui::SameLine(); if (ImGui::SmallButton("x")) rm = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (rm >= 0) { sl->items.erase(sl->items.begin() + rm); lib_.save(); if (curSetlist_ == sl->id) curIndex_ = -1; }
        if (mvIdx >= 0) {
            const int to = mvIdx + mv;
            if (to >= 0 && to < static_cast<int>(sl->items.size())) { std::swap(sl->items[static_cast<size_t>(mvIdx)], sl->items[static_cast<size_t>(to)]); lib_.save(); if (curSetlist_ == sl->id && curIndex_ == mvIdx) curIndex_ = to; }
        }
        if (play >= 0) { loadSetlistItem(sl->id, play); tab_ = Tab::Stage; }
    }
    const float gap2 = ImGui::GetStyle().ItemSpacing.x;
    const float bw = (in - 3 * gap2) / 4.f;
    if (ImGui::Button("+ Adicionar música", ImVec2(bw, 38 * dpi_))) ImGui::OpenPopup("##addsong");
    if (ImGui::BeginPopup("##addsong")) {
        for (auto& s : lib_.songs) {
            ImGui::PushID(static_cast<int>(s.id));
            if (ImGui::Selectable(s.title.c_str())) { SetlistItem it; it.songId = s.id; sl->items.push_back(it); lib_.save(); }
            ImGui::PopID();
        }
        if (lib_.songs.empty()) Caption("Importe músicas na Biblioteca primeiro.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("▶ Tocar setlist", ImVec2(bw, 38 * dpi_)) && !sl->items.empty()) { loadSetlistItem(sl->id, 0); tab_ = Tab::Stage; }
    ImGui::SameLine();
    if (ImGui::Button("Exportar pacote…", ImVec2(bw, 38 * dpi_))) {
        fs::path out = dlg::savePackage(hwnd_, sl->name + ".vsstage");
        if (!out.empty()) { std::string err; if (lib_.exportPackage(out, sl->id, {}, true, &err)) toast("Pacote salvo em " + u8s(out)); else toast(err); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicar", ImVec2(bw, 38 * dpi_))) selSetlist_ = lib_.duplicateSetlist(sl->id);
    ImGui::PushStyleColor(ImGuiCol_Button, col(0x4A1620));
    if (ImGui::Button("Excluir setlist", ImVec2(bw, 0))) confirmDeleteSetlist_ = true;
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::EndChild();
}

// ------------------------------------------------------------------ Modais

void App::drawModals() {
    // ---- importação ----
    if (draft_.open) { ImGui::OpenPopup("Importar música"); draft_.open = false; importOpen_ = true; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.f * dpi_, vp->Size.x - 40), std::min(560.f * dpi_, vp->Size.y - 40)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Importar música", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        Caption(draft_.addToSong >= 0 ? "Adicionar faixas à música" : "Confira o título e o papel de cada faixa (reconhecidos pelo nome do arquivo)");
        if (draft_.addToSong < 0) { ImGui::SetNextItemWidth(-1); ImGui::InputText("##t", draft_.title, sizeof draft_.title); }
        if (ImGui::BeginTable("##imp", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, -48 * dpi_))) {
            ImGui::TableSetupColumn("Arquivo", ImGuiTableColumnFlags_WidthStretch, 5.f);
            ImGui::TableSetupColumn("Papel", ImGuiTableColumnFlags_WidthStretch, 2.f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40 * dpi_);
            ImGui::TableHeadersRow();
            int rm = -1;
            for (size_t i = 0; i < draft_.files.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(u8s(draft_.files[i].filename()).c_str());
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##r", roleInfo(static_cast<Role>(draft_.roles[i])).label)) {
                    for (int r = 0; r < static_cast<int>(Role::Count); ++r)
                        if (ImGui::Selectable(roleInfo(static_cast<Role>(r)).label, r == draft_.roles[i])) draft_.roles[i] = r;
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn(); if (ImGui::SmallButton("x")) rm = static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (rm >= 0) { draft_.files.erase(draft_.files.begin() + rm); draft_.roles.erase(draft_.roles.begin() + rm); draft_.names.erase(draft_.names.begin() + rm); }
        }
        if (ImGui::Button("Importar", ImVec2(140 * dpi_, 36 * dpi_)) && !draft_.files.empty()) {
            std::string err;
            if (draft_.addToSong >= 0) {
                SongEntry* s = lib_.song(draft_.addToSong);
                const size_t before = s ? s->tracks.size() : 0;
                if (lib_.addTracks(draft_.addToSong, draft_.files, &err) && s)
                    for (size_t i = 0; i < draft_.files.size(); ++i) { s->tracks[before + i].role = roleInfo(static_cast<Role>(draft_.roles[i])).id; s->tracks[before + i].name = draft_.names[i]; }
                lib_.save();
                selSong_ = draft_.addToSong;
            } else {
                const int64_t id = lib_.importSong(draft_.files, draft_.title, &err);
                if (id < 0) toast(err);
                else if (SongEntry* s = lib_.song(id)) {
                    for (size_t i = 0; i < s->tracks.size() && i < draft_.roles.size(); ++i) { s->tracks[i].role = roleInfo(static_cast<Role>(draft_.roles[i])).id; s->tracks[i].name = draft_.names[i]; }
                    lib_.save();
                    selSong_ = id;
                    toast("Música importada: " + s->title);
                }
            }
            if (!err.empty()) toast(err);
            draft_ = ImportDraft{};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120 * dpi_, 36 * dpi_))) { draft_ = ImportDraft{}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // ---- confirmações ----
    if (confirmDeleteSong_) { ImGui::OpenPopup("Excluir música?"); confirmDeleteSong_ = false; }
    if (ImGui::BeginPopupModal("Excluir música?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        SongEntry* s = lib_.song(selSong_);
        ImGui::Text("Excluir \"%s\" e os arquivos copiados para o app?", s ? s->title.c_str() : "");
        Caption("Os arquivos originais não são afetados.");
        if (ImGui::Button("Excluir", ImVec2(120 * dpi_, 0))) {
            if (player_.songId == selSong_) player_.clear();
            lib_.removeSong(selSong_); selSong_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120 * dpi_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (confirmDeleteSetlist_) { ImGui::OpenPopup("Excluir setlist?"); confirmDeleteSetlist_ = false; }
    if (ImGui::BeginPopupModal("Excluir setlist?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        Setlist* sl = lib_.setlist(selSetlist_);
        ImGui::Text("Excluir o setlist \"%s\"? (as músicas continuam na biblioteca)", sl ? sl->name.c_str() : "");
        if (ImGui::Button("Excluir", ImVec2(120 * dpi_, 0))) {
            if (curSetlist_ == selSetlist_) { curSetlist_ = -1; curIndex_ = -1; }
            lib_.removeSetlist(selSetlist_); selSetlist_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120 * dpi_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    drawSectionEditor();
}

// ------------------------------------------------------------------ Editor de seções

void App::drawSectionEditor() {
    if (openSectionEditor_) { ImGui::OpenPopup("Seções da música"); openSectionEditor_ = false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.f * dpi_, vp->Size.x - 40), std::min(540.f * dpi_, vp->Size.y - 40)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Seções da música", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    Player& p = player_;
    const int total = std::max(1, p.totalBars());
    ImGui::PushTextWrapPos(0);
    Caption("Defina em qual compasso cada seção começa (compasso 1 = início da grade). A duração de cada seção é calculada automaticamente.");
    ImGui::PopTextWrapPos();
    if (ImGui::BeginTable("##secs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, -50 * dpi_))) {
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Começa no compasso", ImGuiTableColumnFlags_WidthFixed, 290 * dpi_);
        ImGui::TableSetupColumn("Compassos", ImGuiTableColumnFlags_WidthFixed, 90 * dpi_);
        ImGui::TableSetupColumn("Cor", ImGuiTableColumnFlags_WidthFixed, 60 * dpi_);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40 * dpi_);
        ImGui::TableHeadersRow();
        int rm = -1;
        auto sorted = SongMap::normalize(editSections_, total);
        for (size_t i = 0; i < editSections_.size(); ++i) {
            Section& s = editSections_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            inputText("##n", s.name);
            ImGui::TableNextColumn();
            int b = s.startBar + 1;
            if (ImGui::SmallButton("-10")) b -= 10; ImGui::SameLine();
            if (ImGui::SmallButton("-")) b -= 1; ImGui::SameLine();
            ImGui::SetNextItemWidth(50 * dpi_); ImGui::InputInt("##b", &b, 0, 0); ImGui::SameLine();
            if (ImGui::SmallButton("+")) b += 1; ImGui::SameLine();
            if (ImGui::SmallButton("+10")) b += 10;
            s.startBar = std::clamp(b, 1, total) - 1;
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            int len = 0; for (auto& x : sorted) if (x.startBar == s.startBar) len = x.bars;
            ImGui::Text("%d", len);
            ImGui::TableNextColumn();
            float c3[3] = {((s.colorRgb >> 16) & 0xFF) / 255.f, ((s.colorRgb >> 8) & 0xFF) / 255.f, (s.colorRgb & 0xFF) / 255.f};
            if (ImGui::ColorEdit3("##c", c3, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                s.colorRgb = (static_cast<uint32_t>(c3[0] * 255 + 0.5f) << 16) | (static_cast<uint32_t>(c3[1] * 255 + 0.5f) << 8) | static_cast<uint32_t>(c3[2] * 255 + 0.5f);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) rm = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (rm >= 0) editSections_.erase(editSections_.begin() + rm);
    }
    if (ImGui::Button("+ Seção", ImVec2(0, 34 * dpi_))) {
        Section s;
        const size_t n = editSections_.size();
        s.name = SongMap::names()[n % SongMap::names().size()]; s.colorRgb = SongMap::colors()[n % SongMap::colors().size()];
        s.startBar = editSections_.empty() ? 0 : std::min(total - 1, editSections_.back().startBar + 8);
        editSections_.push_back(s);
    }
    ImGui::SameLine();
    if (ImGui::Button("Marcar na posição atual", ImVec2(0, 34 * dpi_))) {
        Section s;
        const size_t n = editSections_.size();
        s.name = SongMap::names()[n % SongMap::names().size()]; s.colorRgb = SongMap::colors()[n % SongMap::colors().size()];
        s.startBar = std::clamp(p.map.barAt(p.position()), 0, total - 1);
        editSections_.push_back(s);
    }
    ImGui::SameLine();
    if (ImGui::Button("Automático (8 em 8)", ImVec2(0, 34 * dpi_))) editSections_ = SongMap::automatic(p.duration(), p.map);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 250 * dpi_);
    if (ImGui::Button("Salvar", ImVec2(120 * dpi_, 34 * dpi_))) {
        if (editSections_.empty()) editSections_ = SongMap::automatic(p.duration(), p.map);
        p.setSections(editSections_);
        persistSong();
        if (curSetlist_ < 0) {}
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110 * dpi_, 34 * dpi_))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace vss
