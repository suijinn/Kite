#include "core/theme/Theme.h"

#include <cmath>
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

// Deliberately achromatic: every surface, every line and every piece of text in
// the dark theme is a pure grey, so nothing on screen carries a colour cast.
// Hierarchy comes from brightness alone - folders are brighter than files,
// selection is a lighter grey than the row under it.
//
// Two things are allowed a hue, and only because brightness cannot carry them.
// The error colour, because "this failed" has to read as such at a glance; and
// the focus colour, because "the keyboard is here" competes with a screen full
// of greys that are all, by design, a shade apart. The blue is the one Windows
// itself uses for the same job - #0078D4 and, on dark, the lighter #4CC2FF that
// WinUI puts on focus rings - so that "this has the keyboard" looks the same
// here as in the rest of the desktop. A desaturated version was tried first and
// read as another grey. Row selection stays grey - it answers a different
// question from focus,
// and giving both the same colour is how they stop being distinguishable.
//
// The selection inside a text field (textSelection) is the other exception, and
// it is blue for a different reason: every text field on the desktop washes its
// selection light blue, and the address bar is read as a text field or not at
// all. A grey wash there is barely a shade off the field itself, which is
// exactly the "am I editing this?" doubt the colour exists to answer.
Theme Theme::Dark() {
    Theme t;
    t.dark = true;
    t.windowBg = Color::hex(0x1B1B1B);
    t.panelBg = Color::hex(0x202020);
    t.listBg = Color::hex(0x1E1E1E);
    t.listBgAlt = Color::hex(0x232323);
    t.border = Color::hex(0x333333);

    t.text = Color::hex(0xD4D4D4);
    t.textDim = Color::hex(0x8C8C8C);
    t.textFolder = Color::hex(0xEDEDED);
    t.textError = Color::hex(0xD98C86);

    t.accent = Color::hex(0x4CC2FF);
    t.accentText = Color::hex(0x141414);
    t.rowHover = Color::hex(0xFFFFFF, 0.08f);
    t.rowSelected = Color::hex(0x3A3A3A);
    t.rowSelectedText = Color::hex(0xF2F2F2);
    // Mid blue rather than the light one the light theme uses: the text drawn
    // over it is the ordinary light grey, so the wash has to stay dark enough
    // to read through.
    t.textSelection = Color::hex(0x2F5D8E);
    t.cursorBorder = Color::hex(0x4CC2FF);
    t.paneFocusBorder = Color::hex(0x4CC2FF);
    t.paneFocusIdle = Color::hex(0x4E4E4E);
    t.paneInactiveScrim = Color::hex(0x000000, 0.22f);

    t.tabActiveBg = Color::hex(0x1E1E1E);
    t.tabInactiveBg = Color::hex(0x191919);
    t.tabActiveText = Color::hex(0xE8E8E8);
    t.tabInactiveText = Color::hex(0x8C8C8C);
    t.sessionActiveBg = Color::hex(0x333333);
    t.scrollThumb = Color::hex(0x4D4D4D);
    t.scrollTrack = Color::hex(0x1E1E1E, 0.0f);
    t.overlayScrim = Color::hex(0x000000, 0.60f);
    t.overlayBg = Color::hex(0x252525);
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

    t.accent = Color::hex(0x0078D4);
    t.accentText = Color::hex(0xFFFFFF);
    t.rowHover = Color::hex(0x000000, 0.07f);
    t.rowSelected = Color::hex(0xD3E0F0);
    t.rowSelectedText = Color::hex(0x10233F);
    t.textSelection = Color::hex(0xCCE8FF);
    t.cursorBorder = Color::hex(0x0078D4);
    t.paneFocusBorder = Color::hex(0x0078D4);
    t.paneFocusIdle = Color::hex(0xBBBBBB);
    t.paneInactiveScrim = Color::hex(0x000000, 0.06f);

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
    ReadColor(ini, sec, "text_selection", &textSelection);
    ReadColor(ini, sec, "cursor_border", &cursorBorder);
    ReadColor(ini, sec, "pane_focus_border", &paneFocusBorder);
    ReadColor(ini, sec, "pane_focus_idle", &paneFocusIdle);
    ReadColor(ini, sec, "pane_inactive_scrim", &paneInactiveScrim);
    ReadColor(ini, sec, "tab_active_bg", &tabActiveBg);
    ReadColor(ini, sec, "tab_inactive_bg", &tabInactiveBg);
    ReadColor(ini, sec, "tab_active_text", &tabActiveText);
    ReadColor(ini, sec, "tab_inactive_text", &tabInactiveText);
    ReadColor(ini, sec, "session_active_bg", &sessionActiveBg);
    ReadColor(ini, sec, "scroll_thumb", &scrollThumb);
    ReadColor(ini, sec, "overlay_bg", &overlayBg);

    rowHeight = ini.GetFloat("ui", "row_height", rowHeight);
    // font_size はここで読まない ─ 読む場所は App だけ（Theme::ApplyIni の注記）。
    uiScale = ini.GetFloat("ui", "scale", uiScale);
    sidebarWidth = ini.GetFloat("ui", "sidebar_width", sidebarWidth);
    tabBarWidth = ini.GetFloat("ui", "tab_bar_width", tabBarWidth);

    const std::string family = ini.GetStr("ui", "font_family");
    if (!family.empty()) fontFamily = family;
    const std::string mono = ini.GetStr("ui", "mono_family");
    if (!mono.empty()) monoFamily = mono;
}

void Theme::Scale(float factor) {
    if (factor <= 0.0f || factor == 1.0f) return;

    fontSize *= factor;

    // 高さは整数 DIP に丸める。行がピクセル境界からずれると、縞模様の境目と
    // カーソルの枠が行ごとに太さを変えて見える。
    rowHeight = std::round(rowHeight * factor);
    headerHeight = std::round(headerHeight * factor);
    tabBarHeight = std::round(tabBarHeight * factor);
    pathBarHeight = std::round(pathBarHeight * factor);
    sessionBarHeight = std::round(sessionBarHeight * factor);
    statusBarHeight = std::round(statusBarHeight * factor);
    // 幅も同じ ─ 中身は「クイックアクセス」のような、縮めれば読めなくなる文字列。
    sidebarWidth = std::round(sidebarWidth * factor);
    // 縦置きタブバーの幅も器のうち。据え置くと、行だけが高くなってタブ名は
    // 同じところで切れたままになる。
    tabBarWidth = std::round(tabBarWidth * factor);
}

}  // namespace kite
