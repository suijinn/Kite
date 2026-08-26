#include <algorithm>
#include <cmath>

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
    // The focus colours and the text-field selection are the deliberate
    // exceptions and are checked below; the row selection is not one of them and
    // has to stay grey, or "selected" and "focused" become the same colour.
    const Theme dark = Theme::Dark();
    const Color neutral[] = { dark.windowBg,    dark.panelBg,        dark.listBg,
                              dark.listBgAlt,   dark.border,         dark.text,
                              dark.textDim,     dark.textFolder,     dark.accentText,
                              dark.rowSelected, dark.rowSelectedText, dark.paneFocusIdle,
                              dark.tabActiveBg, dark.tabInactiveBg,  dark.sessionActiveBg,
                              dark.scrollThumb, dark.overlayBg };
    for (const Color& c : neutral) {
        const float high = std::max(c.r, std::max(c.g, c.b));
        const float low = std::min(c.r, std::min(c.g, c.b));
        // 2/255 of slack: enough for a hand-picked grey, far below a visible tint.
        KITE_EXPECT(high - low < 0.008f);
    }
}

KITE_TEST(theme, the_focus_colours_are_a_blue_that_reads_as_blue) {
    // It has to be blue, because that is what the rest of the desktop means by
    // "this has the keyboard". It also has to be *visibly* blue: a desaturated
    // version of the right hue sat among the greys and read as one more of them,
    // which is the whole failure this guards against. The upper bound only keeps
    // it off pure cyan.
    for (const Theme& t : { Theme::Dark(), Theme::Light() }) {
        const Color focus[] = { t.accent, t.paneFocusBorder, t.cursorBorder };
        for (const Color& c : focus) {
            KITE_EXPECT(c.b > c.r);
            KITE_EXPECT(c.b > c.g);
            const float high = std::max(c.r, std::max(c.g, c.b));
            const float low = std::min(c.r, std::min(c.g, c.b));
            KITE_EXPECT(high - low > 0.25f);
            KITE_EXPECT(high - low < 0.85f);
        }
    }
}

KITE_TEST(theme, the_field_selection_is_blue_and_the_text_still_reads_on_it) {
    // The other hue either theme carries on purpose, and for a different reason
    // than focus: every text field on the desktop washes its selection blue, and
    // a row that is being typed into is read as a field or not at all. A grey
    // wash there sits a shade off the field itself - which is exactly the "am I
    // editing this?" doubt the colour is there to answer.
    for (const Theme& t : { Theme::Dark(), Theme::Light() }) {
        KITE_EXPECT(t.textSelection.b > t.textSelection.r);
        KITE_EXPECT(t.textSelection.b > t.textSelection.g);

        // The text over it keeps its ordinary colour - one DrawText call cannot
        // paint a run in two - so the wash has to stay clear of it in luminance.
        const float sel = (t.textSelection.r + t.textSelection.g + t.textSelection.b) / 3.0f;
        const float fg = (t.text.r + t.text.g + t.text.b) / 3.0f;
        KITE_EXPECT(std::abs(fg - sel) > 0.3f);
    }
}

KITE_TEST(theme, an_inactive_window_drops_the_hue) {
    // Whether Kite has the keyboard at all is read off this colour, so it must
    // not be the accent wearing a different alpha.
    for (const Theme& t : { Theme::Dark(), Theme::Light() }) {
        const float high = std::max(t.paneFocusIdle.r,
                                    std::max(t.paneFocusIdle.g, t.paneFocusIdle.b));
        const float low = std::min(t.paneFocusIdle.r,
                                   std::min(t.paneFocusIdle.g, t.paneFocusIdle.b));
        KITE_EXPECT(high - low < 0.02f);
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

KITE_TEST(theme, the_focus_colours_are_overridable_like_every_other_colour) {
    // A new field is easy to add to Theme and forget in ApplyIni, and the effect
    // - a colour the ini silently refuses to change - is invisible until someone
    // tries it.
    Ini ini;
    ini.Set("theme.dark", "pane_focus_border", "112233");
    ini.Set("theme.dark", "pane_focus_idle", "445566");
    ini.Set("theme.dark", "pane_inactive_scrim", "00000040");

    Theme dark = Theme::Dark();
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.paneFocusBorder.b, 0x33 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(dark.paneFocusIdle.g, 0x55 / 255.0f, 0.001);
    KITE_EXPECT_NEAR(dark.paneInactiveScrim.a, 0x40 / 255.0f, 0.01);
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

KITE_TEST(theme, the_base_font_size_is_not_read_here) {
    // `[ui] font_size` を読む場所は App だけ。ここでも読むと、App が器ごと伸ばす
    // ために掛ける比の上に、文字だけがもう一度大きくなって乗る。
    Ini ini;
    ini.SetFloat("ui", "font_size", 26.0f);

    Theme dark = Theme::Dark();
    dark.ApplyIni(ini);
    KITE_EXPECT_NEAR(dark.fontSize, kDefaultFontSize, 0.001);
}

KITE_TEST(theme, scaling_takes_the_row_up_with_the_text) {
    // The whole point of the method: a row that keeps its height while the text
    // in it grows clips the text, which is exactly the bug a plain font_size
    // knob produces.
    Theme t = Theme::Dark();
    const float font = t.fontSize;
    const float row = t.rowHeight;
    const float splitter = t.splitterWidth;

    t.Scale(1.5f);
    KITE_EXPECT_NEAR(t.fontSize, font * 1.5f, 0.001);
    KITE_EXPECT_NEAR(t.rowHeight, std::round(row * 1.5f), 0.001);
    KITE_EXPECT(t.tabBarHeight > Theme::Dark().tabBarHeight);
    KITE_EXPECT(t.sidebarWidth > Theme::Dark().sidebarWidth);
    // Same for the vertical tab bar: it holds tab names, so it is a container too.
    KITE_EXPECT(t.tabBarWidth > Theme::Dark().tabBarWidth);
    // Nothing sits on the splitter, so it stays the width it was drawn at.
    KITE_EXPECT_NEAR(t.splitterWidth, splitter, 0.001);
}

KITE_TEST(theme, scaling_by_one_or_by_nothing_changes_nothing) {
    Theme t = Theme::Dark();
    const Theme before = t;
    t.Scale(1.0f);
    t.Scale(0.0f);
    t.Scale(-2.0f);
    KITE_EXPECT_NEAR(t.fontSize, before.fontSize, 0.001);
    KITE_EXPECT_NEAR(t.rowHeight, before.rowHeight, 0.001);
}

KITE_TEST(theme, scaling_applies_on_top_of_the_ini) {
    // Order matters: the ini is the authored size, the scale is what the user
    // asked for on top of it. Reading the ini after scaling would throw the
    // scale away.
    Ini ini;
    ini.SetFloat("ui", "row_height", 30.0f);

    Theme t = Theme::Dark();
    t.ApplyIni(ini);
    t.Scale(2.0f);
    KITE_EXPECT_NEAR(t.rowHeight, 60.0f, 0.001);
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
