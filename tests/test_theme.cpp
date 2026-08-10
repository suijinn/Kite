#include <algorithm>

#include "TestFramework.h"
#include "core/theme/Theme.h"

using namespace kite;

KITE_TEST(theme, hex_helper_expands_channels) {
    const Color c = Color::hex(0x336699);
    KITE_EXPECT_NEAR(c.r, 0x33 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(c.g, 0x66 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(c.b, 0x99 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(c.a, 1.0f, 0.001);
}

KITE_TEST(theme, dark_and_light_differ_and_declare_themselves) {
    const Theme dark = Theme::Dark();
    const Theme light = Theme::Light();
    KITE_EXPECT(dark.dark);
    KITE_EXPECT_FALSE(light.dark);
    KITE_EXPECT_NE(dark.listBg.r, light.listBg.r);
}

KITE_TEST(theme, dark_theme_keeps_text_readable_against_its_background) {
    // Not a contrast-ratio check, just a guard against an edit that makes the
    // list unreadable: text and background must sit on opposite sides.
    const Theme dark = Theme::Dark();
    const float bg = (dark.listBg.r + dark.listBg.g + dark.listBg.b) / 3.0f;
    const float fg = (dark.text.r + dark.text.g + dark.text.b) / 3.0f;
    KITE_EXPECT(fg - bg > 0.4f);
}

KITE_TEST(theme, dark_theme_stays_neutral_grey) {
    // The dark default is meant to have no colour cast at all. A tint creeps back
    // in one channel at a time, so check every surface rather than eyeballing it.
    const Theme dark = Theme::Dark();
    const Color neutral[] = { dark.windowBg,   dark.panelBg,        dark.listBg,
                              dark.listBgAlt,  dark.border,         dark.text,
                              dark.textDim,    dark.textFolder,     dark.accent,
                              dark.rowSelected, dark.rowSelectedText, dark.cursorBorder,
                              dark.tabActiveBg, dark.tabInactiveBg,  dark.sessionActiveBg,
                              dark.scrollThumb, dark.overlayBg };
    for (const Color& c : neutral) {
        const float high = std::max(c.r, std::max(c.g, c.b));
        const float low = std::min(c.r, std::min(c.g, c.b));
        // 2/255 of slack: enough for a hand-picked grey, far below a visible tint.
        KITE_EXPECT(high - low < 0.008f);
    }
}

KITE_TEST(theme, ini_overrides_colors_for_the_matching_variant) {
    Ini ini;
    ini.Set("theme.dark", "accent", "FF0000");
    ini.Set("theme.light", "accent", "00FF00");

    Theme dark = Theme::Dark();
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.accent.r, 1.0f, 0.001);
    KITE_EXPECT_NEAR(dark.accent.g, 0.0f, 0.001);

    Theme light = Theme::Light();
    light.ApplyIni(ini);
    KITE_EXPECT_NEAR(light.accent.g, 1.0f, 0.001);
}

KITE_TEST(theme, ini_accepts_a_leading_hash_and_an_alpha_channel) {
    Ini ini;
    ini.Set("theme.dark", "accent", "#204080");
    ini.Set("theme.dark", "row_hover", "FFFFFF80");

    Theme dark = Theme::Dark();
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.accent.r, 0x20 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(dark.rowHover.a, 0x80 / 255.0f, 0.01);
}

KITE_TEST(theme, malformed_colors_leave_the_default_in_place) {
    Ini ini;
    ini.Set("theme.dark", "accent", "not-a-color");

    Theme dark = Theme::Dark();
    const Color before = dark.accent;
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.accent.r, before.r, 0.0001);
}

KITE_TEST(theme, ini_overrides_metrics_and_fonts) {
    Ini ini;
    ini.SetFloat("ui", "row_height", 26.0f);
    ini.Set("ui", "font_family", "Meiryo");

    Theme dark = Theme::Dark();
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.rowHeight, 26.0f, 0.001);
    KITE_EXPECT_EQ(dark.fontFamily, std::string("Meiryo"));
}

KITE_TEST(theme, rect_geometry_helpers) {
    const RectF r = RectF::xywh(10, 20, 100, 50);
    KITE_EXPECT_NEAR(r.w(), 100.0f, 0.001);
    KITE_EXPECT_NEAR(r.h(), 50.0f, 0.001);
    KITE_EXPECT(r.contains(10.0f, 20.0f));
    KITE_EXPECT_FALSE(r.contains(110.0f, 20.0f));  // right edge is exclusive

    const RectF zeroWidth{ 0.0f, 0.0f, 0.0f, 10.0f };
    KITE_EXPECT(zeroWidth.empty());
    KITE_EXPECT_FALSE(r.empty());

    const RectF inset = r.inset(5.0f);
    KITE_EXPECT_NEAR(inset.l, 15.0f, 0.001);
    KITE_EXPECT_NEAR(inset.r, 105.0f, 0.001);
}
