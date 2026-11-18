#pragma once
#include <cmath>
#include <cstdint>
#include <string>

#include "imgui.h"

namespace vss::ui {

constexpr uint32_t kBg = 0x0D0D0F, kPanel = 0x17171B, kPanel2 = 0x202026, kLine = 0x2E2E36, kText = 0xEDEDF0, kDim = 0x8E8E99;
constexpr uint32_t kAccent = 0x00B0FF, kGood = 0x00E676, kWarn = 0xFFC400, kBad = 0xFF1744;

inline ImVec4 col(uint32_t rgb, float a = 1.f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a);
}
inline ImU32 col32(uint32_t rgb, float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(col(rgb, a)); }
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) { return static_cast<uint32_t>(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t) & 0xFF; };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
inline float toDb(float lin) { return lin <= 1e-5f ? -100.f : 20.f * std::log10(lin); }

void applyTheme(float dpiScale);

enum class Icon { Play, Pause, Stop, Prev, Next, Loop, Plus, Minus };
/// Botão quadrado/retangular com ícone desenhado (não depende de glifos da fonte).
bool IconButton(const char* id, Icon icon, ImVec2 size, bool active = false, uint32_t activeColor = kAccent);
/// Botão alternável com texto (Mute/Solo...). Retorna true ao clicar.
bool ToggleButton(const char* label, bool on, ImVec2 size, uint32_t onColor, uint32_t onText = 0x000000);
/// Medidor de nível vertical (0..1 linear, escala em dB de -48 a 0).
void VuMeter(ImVec2 size, float peakLinear);
/// Texto centralizado com corte por reticências dentro da largura.
void TextCentered(const std::string& s, float width, uint32_t color = kText);
/// Rótulo de seção pequeno em cinza.
void Caption(const char* s);
std::string formatTime(double sec);

}  // namespace vss::ui
