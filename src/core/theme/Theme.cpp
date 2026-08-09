#include "core/theme/Theme.h"

#include <cstdlib>

namespace kite {
namespace {

bool ParseHexColor(const std::string& s, Color* out) {
    if (s.empty()) return false;
    size_t i = (s[0] == '#') ? 1 : 0;
    const std::string hex = s.substr(i);
    if (hex.size() != 6 && hex.size() != 8) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(hex.c_str(), &end, 16);
    if (end != hex.c_str() + hex.size()) return false;
    if (hex.size() == 6) {
        *out = Color::hex(static_cast<uint32_t>(v));
    } else {
        *out = Color::hex(static_cast<uint32_t>(v >> 8),
                          static_cast<float>(v & 0xFF) / 255.0f);
    }
    return true;
}

void ReadColor(const Ini& ini, const std::string& sec, const char* key, Color* out) {
    const std::string v = ini.GetStr(sec, key);
    if (!v.empty()) ParseHexColor(v, out);
}

}  // namespace

Theme Theme::Dark() {
    Theme t;
    t.dark = true;
    t.windowBg = Color::hex(0x14161A);
    t.panelBg = Color::hex(0x181B20);
    t.listBg = Color::hex(0x1B1E24);
    t.listBgAlt = Color::hex(0x1E2228);
    t.border = Color::hex(0x2B3038);

    t.text = Color::hex(0xD8DEE9);
    t.textDim = Color::hex(0x7C8695);
    t.textFolder = Color::hex(0x9CC4FF);
    t.textError = Color::hex(0xE88388);

    t.accent = Color::hex(0x3E7BFA);
    t.accentText = Color::hex(0xFFFFFF);
    t.rowHover = Color::hex(0xFFFFFF, 0.05f);
    t.rowSelected = Color::hex(0x2C4A7C);
    t.rowSelectedText = Color::hex(0xEAF1FF);
    t.cursorBorder = Color::hex(0x6E9BFF);
    t.paneFocusBorder = Color::hex(0x3E7BFA);

    t.tabActiveBg = Color::hex(0x1B1E24);
    t.tabInactiveBg = Color::hex(0x15181D);
    t.tabActiveText = Color::hex(0xE6ECF5);
    t.tabInactiveText = Color::hex(0x808A99);
    t.sessionActiveBg = Color::hex(0x263043);
    t.scrollThumb = Color::hex(0x4A515C);
    t.scrollTrack = Color::hex(0x1B1E24, 0.0f);
    t.overlayScrim = Color::hex(0x000000, 0.55f);
    t.overlayBg = Color::hex(0x20242B);
    return t;
}

Theme Theme::Light() {
    Theme t;
    t.dark = false;
    t.windowBg = Color::hex(0xF3F4F6);
    t.panelBg = Color::hex(0xEDEEF1);
    t.listBg = Color::hex(0xFFFFFF);
    t.listBgAlt = Color::hex(0xF7F8FA);
    t.border = Color::hex(0xD4D7DD);

    t.text = Color::hex(0x1F2328);
    t.textDim = Color::hex(0x6B7280);
    t.textFolder = Color::hex(0x1F4FA8);
    t.textError = Color::hex(0xB3261E);

    t.accent = Color::hex(0x2563EB);
    t.accentText = Color::hex(0xFFFFFF);
    t.rowHover = Color::hex(0x000000, 0.05f);
    t.rowSelected = Color::hex(0xCBDDFB);
    t.rowSelectedText = Color::hex(0x10233F);
    t.cursorBorder = Color::hex(0x2563EB);
    t.paneFocusBorder = Color::hex(0x2563EB);

    t.tabActiveBg = Color::hex(0xFFFFFF);
    t.tabInactiveBg = Color::hex(0xE4E6EA);
    t.tabActiveText = Color::hex(0x1F2328);
    t.tabInactiveText = Color::hex(0x656C77);
    t.sessionActiveBg = Color::hex(0xD5E1F7);
    t.scrollThumb = Color::hex(0xB4B9C2);
    t.scrollTrack = Color::hex(0xFFFFFF, 0.0f);
    t.overlayScrim = Color::hex(0x000000, 0.35f);
    t.overlayBg = Color::hex(0xFFFFFF);
    return t;
}

void Theme::ApplyIni(const Ini& ini) {
    const std::string sec = dark ? "theme.dark" : "theme.light";

    ReadColor(ini, sec, "window_bg", &windowBg);
    ReadColor(ini, sec, "panel_bg", &panelBg);
    ReadColor(ini, sec, "list_bg", &listBg);
    ReadColor(ini, sec, "list_bg_alt", &listBgAlt);
    ReadColor(ini, sec, "border", &border);
    ReadColor(ini, sec, "text", &text);
    ReadColor(ini, sec, "text_dim", &textDim);
    ReadColor(ini, sec, "text_folder", &textFolder);
    ReadColor(ini, sec, "text_error", &textError);
    ReadColor(ini, sec, "accent", &accent);
    ReadColor(ini, sec, "accent_text", &accentText);
    ReadColor(ini, sec, "row_hover", &rowHover);
    ReadColor(ini, sec, "row_selected", &rowSelected);
    ReadColor(ini, sec, "row_selected_text", &rowSelectedText);
    ReadColor(ini, sec, "cursor_border", &cursorBorder);
    ReadColor(ini, sec, "pane_focus_border", &paneFocusBorder);
    ReadColor(ini, sec, "tab_active_bg", &tabActiveBg);
    ReadColor(ini, sec, "tab_inactive_bg", &tabInactiveBg);
    ReadColor(ini, sec, "tab_active_text", &tabActiveText);
    ReadColor(ini, sec, "tab_inactive_text", &tabInactiveText);
    ReadColor(ini, sec, "session_active_bg", &sessionActiveBg);
    ReadColor(ini, sec, "scroll_thumb", &scrollThumb);
    ReadColor(ini, sec, "overlay_bg", &overlayBg);

    rowHeight = ini.GetFloat("ui", "row_height", rowHeight);
    fontSize = ini.GetFloat("ui", "font_size", fontSize);
    uiScale = ini.GetFloat("ui", "scale", uiScale);
    sidebarWidth = ini.GetFloat("ui", "sidebar_width", sidebarWidth);

    const std::string family = ini.GetStr("ui", "font_family");
    if (!family.empty()) fontFamily = family;
    const std::string mono = ini.GetStr("ui", "mono_family");
    if (!mono.empty()) monoFamily = mono;
}

}  // namespace kite
