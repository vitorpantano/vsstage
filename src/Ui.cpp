#include "Ui.h"

#include <algorithm>
#include <cstdio>

#include "imgui_internal.h"

namespace vss::ui {

void applyTheme(float s) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0; st.ChildRounding = 8; st.FrameRounding = 6; st.PopupRounding = 8; st.GrabRounding = 5; st.ScrollbarRounding = 8; st.TabRounding = 6;
    st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(10, 6); st.ItemSpacing = ImVec2(8, 8); st.ItemInnerSpacing = ImVec2(6, 6);
    st.ScrollbarSize = 12; st.GrabMinSize = 16; st.ChildBorderSize = 1; st.FrameBorderSize = 0; st.WindowBorderSize = 0;
    auto* c = st.Colors;
    c[ImGuiCol_Text] = col(kText); c[ImGuiCol_TextDisabled] = col(kDim);
    c[ImGuiCol_WindowBg] = col(kBg); c[ImGuiCol_ChildBg] = col(kPanel); c[ImGuiCol_PopupBg] = col(0x121215);
    c[ImGuiCol_Border] = col(kLine); c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = col(kPanel2); c[ImGuiCol_FrameBgHovered] = col(0x2B2B33); c[ImGuiCol_FrameBgActive] = col(0x34343E);
    c[ImGuiCol_Button] = col(kPanel2); c[ImGuiCol_ButtonHovered] = col(0x30303A); c[ImGuiCol_ButtonActive] = col(0x3A3A46);
    c[ImGuiCol_Header] = col(kAccent, 0.25f); c[ImGuiCol_HeaderHovered] = col(kAccent, 0.35f); c[ImGuiCol_HeaderActive] = col(kAccent, 0.5f);
    c[ImGuiCol_SliderGrab] = col(0xE8E8EE); c[ImGuiCol_SliderGrabActive] = col(kAccent);
    c[ImGuiCol_CheckMark] = col(kAccent);
    c[ImGuiCol_Separator] = col(kLine);
    c[ImGuiCol_ScrollbarBg] = col(kBg, 0.f); c[ImGuiCol_ScrollbarGrab] = col(0x3A3A46);
    c[ImGuiCol_TableHeaderBg] = col(kPanel2); c[ImGuiCol_TableBorderLight] = col(kLine); c[ImGuiCol_TableBorderStrong] = col(kLine);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    st.ScaleAllSizes(s);
}

bool IconButton(const char* id, Icon icon, ImVec2 size, bool active, uint32_t activeColor) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? col(activeColor) : col(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? col(mix(activeColor, 0xFFFFFF, 0.15f)) : col(0x30303A));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? col(mix(activeColor, 0xFFFFFF, 0.3f)) : col(0x3A3A46));
    const bool clicked = ImGui::Button(id, size);
    ImGui::PopStyleColor(3);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = std::min(b.x - a.x, b.y - a.y) * 0.24f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fg = active ? col32(0x000000) : col32(kText);
    switch (icon) {
        case Icon::Play: dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r), ImVec2(c.x + r * 1.1f, c.y), fg); break;
        case Icon::Pause: dl->AddRectFilled(ImVec2(c.x - r * 0.9f, c.y - r), ImVec2(c.x - r * 0.25f, c.y + r), fg, 2);
                          dl->AddRectFilled(ImVec2(c.x + r * 0.25f, c.y - r), ImVec2(c.x + r * 0.9f, c.y + r), fg, 2); break;
        case Icon::Stop: dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fg, 2); break;
        case Icon::Prev: dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r), fg);
                         dl->AddTriangleFilled(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(c.x - r * 0.5f, c.y), fg); break;
        case Icon::Next: dl->AddRectFilled(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r, c.y + r), fg);
                         dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r), ImVec2(c.x + r * 0.5f, c.y), fg); break;
        case Icon::Loop: dl->AddCircle(c, r, fg, 24, 2.2f);
                         dl->AddTriangleFilled(ImVec2(c.x + r * 0.2f, c.y - r * 1.5f), ImVec2(c.x + r * 0.2f, c.y - r * 0.5f), ImVec2(c.x + r * 1.1f, c.y - r), fg); break;
        case Icon::Plus: dl->AddRectFilled(ImVec2(c.x - r, c.y - 1.5f), ImVec2(c.x + r, c.y + 1.5f), fg);
                         dl->AddRectFilled(ImVec2(c.x - 1.5f, c.y - r), ImVec2(c.x + 1.5f, c.y + r), fg); break;
        case Icon::Minus: dl->AddRectFilled(ImVec2(c.x - r, c.y - 1.5f), ImVec2(c.x + r, c.y + 1.5f), fg); break;
    }
    return clicked;
}

bool ToggleButton(const char* label, bool on, ImVec2 size, uint32_t onColor, uint32_t onText) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? col(onColor) : col(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? col(mix(onColor, 0xFFFFFF, 0.15f)) : col(0x30303A));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? col(mix(onColor, 0xFFFFFF, 0.3f)) : col(0x3A3A46));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? col(onText) : col(kText));
    const bool c = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return c;
}

void VuMeter(ImVec2 size, float peak) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), col32(0x08080A), 3);
    const float db = toDb(peak);
    const float frac = std::clamp((db + 48.f) / 48.f, 0.f, 1.f);
    const int segs = 24;
    const float sh = size.y / segs;
    for (int i = 0; i < segs; ++i) {
        const float segTop = static_cast<float>(i + 1) / segs;
        if (segTop > frac + 0.001f) continue;
        const float pos = static_cast<float>(i) / segs;
        const uint32_t c = pos > 0.9f ? kBad : (pos > 0.72f ? kWarn : kGood);
        const float y1 = p.y + size.y - i * sh, y0 = y1 - sh + 1.5f;
        dl->AddRectFilled(ImVec2(p.x + 1, y0), ImVec2(p.x + size.x - 1, y1 - 0.5f), col32(c), 1);
    }
}

void TextCentered(const std::string& s, float width, uint32_t color) {
    std::string t = s;
    float w = ImGui::CalcTextSize(t.c_str()).x;
    while (w > width && t.size() > 3) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // não cortar no meio de UTF-8
        w = ImGui::CalcTextSize((t + "…").c_str()).x;
    }
    if (t.size() != s.size()) t += "…";
    const float x = ImGui::GetCursorPosX() + std::max(0.f, (width - ImGui::CalcTextSize(t.c_str()).x) * 0.5f);
    ImGui::SetCursorPosX(x);
    ImGui::PushStyleColor(ImGuiCol_Text, col(color));
    ImGui::TextUnformatted(t.c_str());
    ImGui::PopStyleColor();
}

void Caption(const char* s) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(kDim));
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
}

std::string formatTime(double sec) {
    if (sec < 0) sec = 0;
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(sec) / 60, static_cast<int>(sec) % 60);
    return b;
}

}  // namespace vss::ui
