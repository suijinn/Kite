// Kite - colors and metrics. Fully data driven so a theme is just an ini file.
#pragma once

#include <string>

#include "core/base/Ini.h"
#include "core/base/Types.h"

namespace kite {

struct Theme {
    // Surfaces
    Color windowBg;
    Color panelBg;      // sidebar, bars
    Color listBg;
    Color listBgAlt;    // zebra striping
    Color border;

    // Text
    Color text;
    Color textDim;
    Color textFolder;
    Color textError;

    // Interaction
    Color accent;
    Color accentText;
    Color rowHover;
    Color rowSelected;
    Color rowSelectedText;
    Color cursorBorder;      // focused-row outline
    Color paneFocusBorder;

    // Chrome
    Color tabActiveBg;
    Color tabInactiveBg;
    Color tabActiveText;
    Color tabInactiveText;
    Color sessionActiveBg;
    Color scrollThumb;
    Color scrollTrack;
    Color overlayScrim;
    Color overlayBg;

    // Metrics, in DIPs
    float rowHeight = 22.0f;
    float headerHeight = 24.0f;
    float tabBarHeight = 28.0f;
    float pathBarHeight = 26.0f;
    float sessionBarHeight = 26.0f;
    float statusBarHeight = 22.0f;
    float sidebarWidth = 190.0f;
    float splitterWidth = 4.0f;
    float fontSize = 13.0f;
    float uiScale = 1.0f;

    std::string fontFamily = "Yu Gothic UI";
    std::string monoFamily = "Consolas";

    bool dark = true;

    static Theme Dark();
    static Theme Light();

    // Reads [theme.dark] / [theme.light] and [ui] from settings.ini.
    void ApplyIni(const Ini& ini);
};

}  // namespace kite
