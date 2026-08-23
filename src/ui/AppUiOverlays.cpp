// The full-window overlays: the shortcut sheet, the shortcut editor, the
// settings screen, and the two choosers (places and commands).
//
// Split out of AppUi.cpp for size alone. They belong together because they are
// one screen with different rows: the panel, the field, the row rhythm and the
// modal manners all come from PaintPickerFrame, and a rule added to one of them
// alone is how the two stop opening at the same size.

#include <algorithm>
#include <cmath>

#include "core/base/Version.h"
#include "core/input/Commands.h"
#include "ui/AppUi.h"
#include "ui/Glyphs.h"

namespace kite::ui {
namespace {

// How wide a filtered chooser's panel is allowed to get (the bookmark list, the
// command palette). One number for both: they are the same screen with different
// rows, and a palette that changed size on the way to the bookmark list would
// move the field being typed into.
constexpr float kPickerMaxWidth = 860.0f;

// A hint line that names the chord the shortcut editor is on right now.
//
// Two screens point at that editor, and neither may spell the chord out: the
// default moves (it has), and the user can rebind or clear it. Asking the keymap
// keeps both true. An unbound command gets the "_unbound" sentence instead of a
// blank where a chord belongs - the same care the palette takes with its own
// unbound rows.
std::string KeySettingsHint(const Strings& str, const KeyMap& keys, const char* key) {
    const std::string chord = keys.ChordText(Cmd::ShowKeySettings);
    if (chord.empty()) return str.Get(std::string(key) + "_unbound");
    return str.Format(key, { chord });
}

}  // namespace

void AppUi::PaintKeyHelp(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    const KeyMap& km = app_.keys();

    r.FillRect(area, th.overlayScrim);

    const RectF panel = area.inset(std::max(24.0f, area.w() * 0.06f),
                                   std::max(24.0f, area.h() * 0.06f));
    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);

    // Three pieces share this line, and each of them used to be handed a rect
    // reaching the far side of it. Wide enough, that reads as three columns;
    // narrow enough, they are drawn on top of each other and none of the three
    // can be read. Each gets only the room left over by the one before it, and
    // what does not fit is left out rather than piled on.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 40.0f };
    const std::string title = str.Get("ui.key_help_title");
    const float titleW = r.MeasureText(title, FontRole::UiBold) + kPad * 2.0f;

    // The chord is asked for, never written down: this sheet used to name
    // Ctrl+F1 in the string table, so it went on advertising that key after the
    // default moved - and it was wrong all along for anyone who had rebound the
    // editor. Unbound has its own sentence rather than an empty gap where a
    // chord should be (KeyMap.cpp's note on the defaults table).
    const std::string hint = KeySettingsHint(str, app_.keys(), "ui.key_help_hint");
    const float hintW = r.MeasureText(hint, FontRole::UiSmall);
    float rest = titleBox.r;
    if (titleW + hintW <= titleBox.w()) {
        r.DrawText(hint, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b }, th.textDim,
                   FontRole::UiSmall, TextAlign::Right);
        rest = titleBox.r - hintW - kPad;
    }

    // This panel covers the session bar, and with it the build stamp. It comes
    // along here because the overlay is the closest thing Kite has to an about
    // box - and it is the first thing dropped, being the one nobody came for.
    const std::string stamp = std::string("Kite ") + version::kDisplay;
    if (titleBox.l + titleW + r.MeasureText(stamp, FontRole::UiSmall) <= rest) {
        r.DrawText(stamp, { titleBox.l + titleW, titleBox.t, rest, titleBox.b },
                   th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);
    }
    r.DrawText(title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);

    // Never inverted, however little room the window leaves: the panel is a
    // fraction of a window that has no minimum size, and a clip whose bottom is
    // above its top is not a small clip.
    const RectF content = { panel.l + 20.0f, titleBox.b + 4.0f,
                            std::max(panel.l + 21.0f, panel.r - 20.0f),
                            std::max(titleBox.b + 5.0f, panel.b - 12.0f) };
    r.PushClip(content);

    // Flatten the command table into printable lines first, so column breaking
    // is a simple index calculation instead of interleaved bookkeeping.
    struct Line {
        bool header = false;
        bool spacer = false;
        std::string label;
        std::string chord;
    };
    std::vector<Line> lines;
    lines.reserve(AllCommands().size() + 24);

    CmdGroup lastGroup = CmdGroup::Count;
    for (const CommandInfo& info : AllCommands()) {
        if (info.group != lastGroup) {
            lastGroup = info.group;
            if (!lines.empty()) lines.push_back({ false, true, {}, {} });
            lines.push_back({ true, false, str.Get(GroupLabelKey(info.group)), {} });
        }
        // Every chord, not the first one: a command with two keys on it that
        // only ever printed one made the second impossible to check from here.
        lines.push_back({ false, false, str.Label(info.labelKey), km.ChordText(info.id) });
    }

    // The chord column is as wide as the widest line of chords actually needs,
    // and the number of columns follows from that. A fixed width was fine while
    // every cell held one chord; "Ctrl+Shift+Tab, Ctrl+PageUp" does not fit in
    // it, and a cheat sheet that cuts the answer in half is worse than no sheet.
    float widest = 0.0f;
    for (const Line& line : lines) {
        if (!line.chord.empty()) widest = std::max(widest, r.MeasureText(line.chord, FontRole::Mono));
    }
    // Past this, one long line would push the whole sheet down to two columns.
    widest = std::min(widest, 210.0f);

    const int columns = std::clamp(static_cast<int>(content.w() / (widest + 160.0f)), 1, 4);
    const float colW = content.w() / static_cast<float>(columns);

    // How tall the sheet has to be for a given set of lines, with the group
    // headings kept off the bottom of a column.
    const auto reflow = [&columns](std::vector<Line>& all) {
        int rows = (static_cast<int>(all.size()) + columns - 1) / columns;
        for (int i = rows - 1; i < static_cast<int>(all.size()); i += rows) {
            if (all[i].header) all.insert(all.begin() + i, { false, true, {}, {} });
        }
        return (static_cast<int>(all.size()) + columns - 1) / columns;
    };

    // Shrink the leading rather than scroll - but only down to where the glyphs
    // still clear each other. A small window used to drive this to a couple of
    // pixels, and the sheet became one solid block of overlapping text.
    const float minLine = r.LineHeight(FontRole::UiSmall) * 0.85f;
    int perColumn = reflow(lines);
    float lineH = std::clamp(content.h() / static_cast<float>(std::max(1, perColumn)), minLine,
                             18.0f);

    if (static_cast<float>(perColumn) * lineH > content.h()) {
        // Still over. The blank line between groups is the cheapest thing on the
        // sheet, so it goes before anything anybody came here to read does.
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [](const Line& l) { return l.spacer; }),
                    lines.end());
        perColumn = reflow(lines);
        lineH = std::clamp(content.h() / static_cast<float>(std::max(1, perColumn)), minLine,
                           18.0f);
    }

    // Whatever is left over is reachable with the wheel. Cutting it off silently
    // would be the sheet answering half the question and saying so nowhere.
    const int shownRows = std::max(1, static_cast<int>(content.h() / lineH));
    keyHelpScroll_ = std::clamp(keyHelpScroll_, 0, std::max(0, perColumn - shownRows));

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (line.spacer) continue;

        const int column = std::min(columns - 1, static_cast<int>(i) / perColumn);
        const int rowInColumn = static_cast<int>(i) % perColumn - keyHelpScroll_;
        if (rowInColumn < 0 || rowInColumn >= shownRows) continue;

        const float x = content.l + static_cast<float>(column) * colW;
        const float y = content.t + static_cast<float>(rowInColumn) * lineH;
        const RectF row = { x, y, x + colW - 14.0f, y + lineH };

        if (line.header) {
            r.DrawText(line.label, row, th.accent, FontRole::UiBold, TextAlign::Left);
            continue;
        }
        const float chordW = std::min(widest + 2.0f, row.w() * 0.6f);
        r.DrawText(line.label, { row.l, row.t, row.r - chordW - 6.0f, row.b }, th.text,
                   FontRole::UiSmall, TextAlign::Left);
        r.DrawText(line.chord.empty() ? "-" : line.chord, { row.r - chordW, row.t, row.r, row.b },
                   line.chord.empty() ? th.textDim.alpha(0.4f) : th.textDim, FontRole::Mono,
                   TextAlign::Right);
    }

    // The same thin mark the vertical tab bar uses, in the gutter every column
    // already leaves at its right. Without it a sheet with rows below the fold
    // looks exactly like one showing everything.
    PaintThinScrollbar(r, { content.r - 3.0f, content.t, content.r, content.b }, perColumn,
                       shownRows, keyHelpScroll_);
    r.PopClip();
}

void AppUi::PaintKeySettings(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    KeyEditor& editor = app_.keyEditor();

    r.FillRect(area, th.overlayScrim);

    // One column of rows, so a wide window gets a centred panel rather than a
    // sheet of mostly empty space.
    const float width = std::clamp(area.w() - 48.0f, 160.0f, 640.0f);
    const float height = std::max(120.0f, area.h() - std::max(32.0f, area.h() * 0.08f) * 2.0f);
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, Hit::KeyPanel);

    // Title on the left, what is being searched for on the right. Both used to
    // own the whole line, which is fine until the window is narrow enough for
    // them to meet in the middle - so the second one only gets what the first
    // leaves, and drops out entirely rather than landing on top of it.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    const std::string title = str.Get("ui.key_settings_title");
    const float titleW = r.MeasureText(title, FontRole::UiBold) + kPad * 2.0f;
    r.DrawText(title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);

    // 変換中の文字列も続けて出す。ここは入力欄の形をしていないので下線も引かないが、
    // 黙っていると «キーが効いていない» と見分けが付かない ─ ラベルは日本語なので、
    // この絞り込みこそ IME で打たれる。
    const std::string typed = editor.filter() + app_.composition().text;
    const std::string search = typed.empty()
                                   ? str.Get("ui.key_settings_search_hint")
                                   : str.Format("ui.key_settings_filter", { typed });
    if (titleW + r.MeasureText(search, FontRole::UiSmall) <= titleBox.w()) {
        r.DrawText(search, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b },
                   typed.empty() ? th.textDim.alpha(0.6f) : th.text, FontRole::UiSmall,
                   TextAlign::Right);
    }

    // The line under the title carries whatever the last action did - which
    // command lost a chord, above all. Silence there means nothing happened.
    const RectF messageBox = { panel.l + 20.0f, titleBox.b, panel.r - 20.0f, titleBox.b + 20.0f };
    if (editor.capturing()) {
        r.DrawText(str.Get("ui.key_settings_hint_capture"), messageBox, th.accent, FontRole::UiSmall,
                   TextAlign::Left);
    } else if (!editor.message().empty()) {
        r.DrawText(editor.message(), messageBox, th.text, FontRole::UiSmall, TextAlign::Left);
    } else {
        r.DrawText(str.Format("ui.key_settings_count",
                              { std::to_string(editor.commandCount()) }),
                   messageBox, th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);
    }

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(str.Get("ui.key_settings_hint"), footer, th.textDim, FontRole::UiSmall,
               TextAlign::Left);

    const RectF body = { panel.l + 12.0f, messageBox.b + 4.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    const float rowH = th.rowHeight;
    const int pageRows = std::max(1, static_cast<int>((body.h() - 4.0f) / rowH));
    // Told every frame: the panel is sized from the window, so the number of
    // rows a PageDown should cover is only known here.
    editor.SetPageRows(pageRows);

    const std::vector<KeyEditor::Row>& rows = editor.rows();
    const int first = editor.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + pageRows - 1);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const KeyEditor::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        if (row.header) {
            r.DrawText(row.label, box.inset(6.0f, 0.0f), th.accent, FontRole::UiBold,
                       TextAlign::Left);
            continue;
        }

        const bool selected = (i == editor.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        // The mouse way to add a chord instead of replacing one. The row itself
        // and the double click are both "replace", so adding needs a target of
        // its own - and its space is held on every row rather than made when the
        // pointer arrives, or the chord beside it would shift as you cross it.
        const RectF add = { box.r - 26.0f, box.t + 5.0f, box.r - 10.0f, box.b - 5.0f };

        // Wide enough for two or three chords side by side. The labels are short
        // and the panel is not: what gets cut off here cannot be read, and a
        // shortcut nobody can read is one nobody can pick out and remove.
        const float chordW = std::min(240.0f, box.w() * 0.5f);
        r.DrawText(row.label, { box.l + 10.0f, box.t, add.l - chordW - 14.0f, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        const bool capturingHere = selected && editor.capturing();
        const std::string chordText = capturingHere ? str.Get("ui.key_settings_capture")
                                      : row.chords.empty() ? str.Get("ui.key_settings_none")
                                                           : row.chords;
        const Color chordColor = capturingHere            ? th.accent
                                 : row.chords.empty()     ? th.textDim.alpha(0.45f)
                                 : selected               ? th.rowSelectedText
                                                          : th.textDim;
        const float chordRight = add.l - 6.0f;

        // One chord of several, marked out inside the line rather than drawn as
        // its own control: the mark goes under the text, the way the selection in
        // an input field does, so the row still reads as one answer. The pieces
        // are measured separately only to place it - the font is monospaced here,
        // so the parts do add up to the whole.
        std::vector<RectF> chips;
        if (!capturingHere && selected && row.chordTexts.size() > 1) {
            const float sep = r.MeasureText(", ", FontRole::Mono);
            float x = chordRight - r.MeasureText(row.chords, FontRole::Mono);
            for (size_t c = 0; c < row.chordTexts.size(); ++c) {
                const float w = r.MeasureText(row.chordTexts[c], FontRole::Mono);
                const RectF chip = { x - 3.0f, box.t + 4.0f, x + w + 3.0f, box.b - 4.0f };
                if (static_cast<int>(c) == editor.chordCursor()) {
                    r.FillRoundRect(chip, 3.0f, th.accent.alpha(0.28f));
                } else if (PointerOver(chip)) {
                    r.FillRoundRect(chip, 3.0f, th.rowHover);
                }
                chips.push_back(chip);
                x += w + sep;
            }
        }

        r.DrawText(chordText, { chordRight - chordW, box.t, chordRight, box.b }, chordColor,
                   FontRole::Mono, TextAlign::Right);

        Add(box, Hit::KeyRow, i);
        // After the row, so the pointer finds the chord rather than the line it
        // sits on: Pick answers with the last thing registered.
        for (size_t c = 0; c < chips.size(); ++c) Add(chips[c], Hit::KeyChord, static_cast<int>(c));

        // Drawn faint on the row being pointed at or selected, the way the tab
        // cross is: a plus on all forty rows at once reads as a column of
        // buttons rather than a list of shortcuts. While the row is waiting for
        // a key it is gone - there is nothing to add to a capture in progress.
        if (!capturingHere && (selected || PointerOver(box))) {
            const bool overAdd = PointerOver(add);
            if (overAdd) r.FillRoundRect(add.inset(-2.0f), 3.0f, th.rowHover);
            glyph::Plus(r, add,
                        overAdd    ? th.text
                        : selected ? th.rowSelectedText.alpha(0.7f)
                                   : th.textDim.alpha(0.55f),
                        1.2f);
            Add(add, Hit::KeyAdd, i);
        }
    }
    r.PopClip();

    PaintPickerScrollbar(r, body, static_cast<int>(rows.size()), pageRows, first);
}

// The settings screen.
//
// Same panel, same row rhythm and the same modal manners as the shortcut editor
// - the two are siblings, and one of them being a different kind of window would
// only be something else to learn. What differs is what a row holds: a value
// with an arrow on each side, rather than a key combination to capture.
void AppUi::PaintSettings(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    SettingsEditor& editor = app_.settingsEditor();

    r.FillRect(area, th.overlayScrim);

    const std::vector<SettingsEditor::Row>& rows = editor.rows();
    const float rowH = th.rowHeight;
    const float chrome = 38.0f + 20.0f + 8.0f + 26.0f + 12.0f;  // title, note, rule, footer, pad
    const float wanted = chrome + rowH * static_cast<float>(rows.size());

    const float width = std::clamp(area.w() - 48.0f, 200.0f, 560.0f);
    const float height = std::min(wanted, std::max(120.0f, area.h() - 48.0f));
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, Hit::SettingsPanel);

    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    r.DrawText(str.Get("ui.settings_title"), titleBox, th.text, FontRole::UiBold, TextAlign::Left);

    // Where the answers end up. A settings screen that writes a file the user
    // may not have noticed should say which file, once, where it is read.
    const RectF noteBox = { panel.l + 20.0f, titleBox.b, panel.r - 20.0f, titleBox.b + 20.0f };
    r.DrawText(str.Get(app_.standalone() ? "ui.settings_no_save" : "ui.settings_file"), noteBox,
               th.textDim.alpha(0.7f), FontRole::UiSmall, TextAlign::Left);

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(KeySettingsHint(str, app_.keys(), "ui.settings_hint"), footer, th.textDim,
               FontRole::UiSmall, TextAlign::Left);

    const RectF body = { panel.l + 12.0f, noteBox.b + 4.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    r.PushClip(body);
    for (size_t i = 0; i < rows.size(); ++i) {
        const SettingsEditor::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };
        if (box.t >= body.b) break;

        if (row.header) {
            r.DrawText(row.label, box.inset(6.0f, 0.0f), th.accent, FontRole::UiBold,
                       TextAlign::Left);
            continue;
        }

        const bool selected = (static_cast<int>(i) == editor.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        const float valueW = std::min(230.0f, box.w() * 0.55f);
        const RectF valueBox = { box.r - valueW, box.t, box.r - 8.0f, box.b };
        const RectF prev = { valueBox.l, valueBox.t, valueBox.l + 14.0f, valueBox.b };
        const RectF next = { valueBox.r - 14.0f, valueBox.t, valueBox.r, valueBox.b };

        r.DrawText(row.label, { box.l + 10.0f, box.t, valueBox.l - 8.0f, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        // The arrows are the whole affordance: without them a value on the right
        // reads as a report rather than as something this row can change.
        const Color edge = th.textDim.alpha(0.35f);
        glyph::ChevronLeft(r, prev, row.atFirst ? edge : th.textDim);
        glyph::ChevronRight(r, next, row.atLast ? edge : th.textDim);
        r.DrawText(row.value, { prev.r, box.t, next.l, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Center);

        Add(box, Hit::SettingsRow, static_cast<int>(i));
        Add(prev, Hit::SettingsPrev, static_cast<int>(i));
        Add(next, Hit::SettingsNext, static_cast<int>(i));
    }
    r.PopClip();
}

// Clicks while the settings screen is up. Nothing behind it is reachable, the
// same as the shortcut editor.
bool AppUi::HandleSettingsClick(const MouseEvent& e) {
    // A confirmation raised by a row owns the screen until it is answered, the
    // same way it owns the keyboard (App::OnKey). A click that moved the cursor
    // or nudged another row underneath would leave the pending Yes belonging to
    // a question nobody can see any more.
    if (app_.prompt().isConfirm()) {
        app_.host().Invalidate();
        return true;
    }
    const Region* region = Pick(e.x, e.y);
    if (region && (region->kind == Hit::SettingsRow || region->kind == Hit::SettingsPrev ||
                   region->kind == Hit::SettingsNext)) {
        SettingsEditor& editor = app_.settingsEditor();
        editor.SelectRow(region->index);
        if (e.button == 0) {
            // The arrows step; the rest of the row cycles. Pressing a row over
            // and over has to keep doing something, so that one wraps - the same
            // split the keyboard makes between the arrow keys and Enter.
            if (region->kind == Hit::SettingsPrev) {
                editor.Adjust(-1, app_.strings());
            } else if (region->kind == Hit::SettingsNext) {
                editor.Adjust(1, app_.strings());
            } else {
                editor.Adjust(1, app_.strings(), true);
            }
            app_.ApplyPendingSetting();
        }
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::SettingsPanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowSettings);
    return true;
}

// One filtered chooser's worth of chrome: the panel, its title and count, the
// field being typed into, and the rect the rows go in.
//
// The places list (Ctrl+P) and the command palette (Ctrl+Shift+P) are the same
// screen with different rows, so the frame is drawn once here rather than twice.
// PickerList already shares the counting behind them, and the reason is the same
// one the tab bar has for sharing its wrapping between the two orientations: two
// copies of a rule mean one of them is always the stale one.
//
// Two things this frame decides, both learned from the palette:
//
//   * **The window alone decides the panel, so every chooser is the same size.**
//     Not the row count, filtered or otherwise. Sized to its rows, a panel crept
//     towards the centre of the window on every keystroke - and the two screens
//     ended up different heights, so choosing "bookmark.list" from the palette
//     moved the field out from under the fingers that had just typed into it. The
//     field is at one place in this app, and it stays there.
//   * **The filter is a field, not small print in the title row.** It is the one
//     thing on the panel being edited, so it gets the body font, a caret, and a
//     border in the accent colour - the same "the keyboard is here" the address
//     bar draws. The caret shows on an empty field too: nothing else says the
//     panel is waiting to be typed into.
AppUi::PickerFrame AppUi::PaintPickerFrame(Renderer& r, const RectF& area,
                                           const PickerChrome& chrome, Hit panelHit) {
    const Theme& th = app_.theme();
    const float rowH = th.rowHeight;
    // Taller than a list row: this one holds text being typed, and a row is the
    // height of text being read. Derived from the row height so the font scale
    // reaches it, the way it reaches everything else that holds letters.
    const float fieldH = std::max(30.0f, rowH * 1.6f);
    const float width = std::clamp(area.w() - 48.0f, 200.0f, kPickerMaxWidth);
    const float height = std::max(140.0f, area.h() - 48.0f);
    const float left = std::round(area.center().x - width * 0.5f);
    const float top = std::round(area.center().y - height * 0.5f);
    const RectF panel = { left, top, left + width, top + height };

    r.FillRoundRect(panel, 8.0f, th.overlayBg);
    r.StrokeRect(panel, th.border, 1.0f);
    Add(panel, panelHit);

    // Title on the left, how many of how many on the right - and the count only
    // gets what the title leaves, dropping out rather than landing on top of it
    // when the window is narrow. With a filter typed, the count is the only thing
    // that says the rest are still there.
    const RectF titleBox = { panel.l + 20.0f, panel.t + 8.0f, panel.r - 20.0f, panel.t + 38.0f };
    const float titleW = r.MeasureText(chrome.title, FontRole::UiBold) + kPad * 2.0f;
    r.DrawText(chrome.title, { titleBox.l, titleBox.t, titleBox.l + titleW, titleBox.b }, th.text,
               FontRole::UiBold, TextAlign::Left);
    if (titleW + r.MeasureText(chrome.count, FontRole::UiSmall) <= titleBox.w()) {
        r.DrawText(chrome.count, { titleBox.l + titleW, titleBox.t, titleBox.r, titleBox.b },
                   th.textDim.alpha(0.8f), FontRole::UiSmall, TextAlign::Right);
    }

    const RectF field = { panel.l + 16.0f, titleBox.b, panel.r - 16.0f, titleBox.b + fieldH };
    const RectF fieldBox = field.inset(0.0f, 3.0f);
    r.FillRoundRect(fieldBox, 4.0f, th.listBg);
    r.StrokeRect(fieldBox, th.accent, 1.0f);

    // The field itself goes through the one routine every field in Kite goes
    // through - text, selection, composition, caret. A chooser's box is 1.6 rows
    // tall, so the caret stops a little further in than it does on a row.
    PaintTextField(r, fieldBox.inset(10.0f, 0.0f), *chrome.field, FontRole::Ui, 5.0f,
                   chrome.placeholder, chrome.prefixLen);

    const RectF footer = { panel.l + 20.0f, panel.b - 26.0f, panel.r - 20.0f, panel.b - 6.0f };
    r.DrawText(chrome.hint, footer, th.textDim, FontRole::UiSmall, TextAlign::Left);

    const RectF body = { panel.l + 12.0f, field.b + 8.0f, panel.r - 12.0f, footer.t - 4.0f };
    r.FillRect({ body.l, body.t, body.r, body.t + 1.0f }, th.border);

    PickerFrame frame;
    frame.panel = panel;
    frame.body = body;
    frame.pageRows = std::max(1, static_cast<int>((body.h() - 4.0f) / rowH));
    return frame;
}

// The thin marker beside a list of rows: the vertical tab bar, the F1 sheet, the
// shortcut editor and both choosers all draw the same one. Cannot be grabbed, the
// same as the listing's own bar (ROADMAP P3-11 makes both draggable at once).
//
// Nothing is drawn when everything is already on screen - a marker that never
// moves says only that there is nothing to move to, which the rows say better.
void AppUi::PaintThinScrollbar(Renderer& r, const RectF& track, int rows, int pageRows,
                               int first) {
    if (rows <= pageRows) return;
    const float ratio = static_cast<float>(pageRows) / static_cast<float>(rows);
    // Never taller than its own track: the minimum is there so the thumb stays
    // findable, not so it can hang out of the bottom of a very short bar.
    const float thumbH = std::min(track.h(), std::max(24.0f, track.h() * ratio));
    const float t = static_cast<float>(first) / static_cast<float>(std::max(1, rows - pageRows));
    const float top = track.t + (track.h() - thumbH) * t;
    r.FillRoundRect({ track.l, top, track.r, top + thumbH }, 1.5f, app_.theme().scrollThumb);
}

// A chooser's rows share their panel with the field above them, so the marker
// runs down the inside edge of the body rather than the panel.
void AppUi::PaintPickerScrollbar(Renderer& r, const RectF& body, int rows, int pageRows,
                                 int first) {
    PaintThinScrollbar(r, { body.r - 4.0f, body.t + 3.0f, body.r - 1.0f, body.b - 3.0f }, rows,
                       pageRows, first);
}

// The list of places (Ctrl+P).
//
// A sibling of the shortcut editor, the settings screen and the command palette:
// same panel, same row rhythm, same modal manners. It exists because the numbered
// shortcuts stop at eight - past that, this is the only way to a bookmark or a tab
// without the mouse. A row holds a name, where it goes, which kind of place it is,
// and the number key that would have reached it.
void AppUi::PaintPlaces(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    PlacePicker& picker = app_.placePicker();

    r.FillRect(area, th.overlayScrim);

    const std::vector<PlacePicker::Row>& rows = picker.rows();
    const float rowH = th.rowHeight;

    PickerChrome chrome;
    chrome.title = str.Get("ui.goto_title");
    chrome.count =
        str.Format("ui.goto_count",
                   { std::to_string(rows.size()) + " / " + std::to_string(picker.total()) });
    chrome.field = &picker.field();
    chrome.placeholder = str.Get("ui.goto_search_hint");
    chrome.hint = str.Get("ui.goto_hint");
    const PickerFrame frame = PaintPickerFrame(r, area, chrome, Hit::PlacePanel);
    const RectF body = frame.body;
    // Told every frame: the panel is sized from the window, so the number of rows
    // a PageDown should cover is only known here.
    picker.SetPageRows(frame.pageRows);

    if (rows.empty()) {
        r.DrawText(str.Get("ui.goto_empty"), body.inset(10.0f, 8.0f),
                   th.textDim.alpha(0.7f), FontRole::Ui, TextAlign::Left);
        return;
    }

    const int first = picker.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + frame.pageRows - 1);

    // The kind column is measured over the rows about to be drawn, the way the
    // palette measures its own: a fixed width cuts the longest label in half.
    float kindCol = 0.0f;
    for (int i = first; i <= last; ++i) {
        kindCol = std::max(kindCol, r.MeasureText(rows[i].kindLabel, FontRole::UiSmall));
    }
    kindCol = std::min(kindCol, 140.0f);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const PlacePicker::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        const bool selected = (i == picker.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        // The number key that also reaches this row, on the eight that have one.
        // A wide enough row only: on a narrow one the name is what was come for,
        // and the shortcut is the piece that can be looked up elsewhere.
        float right = box.r - 10.0f;
        if (!row.chords.empty() && box.w() > 300.0f) {
            const float w = std::min(150.0f, r.MeasureText(row.chords, FontRole::Mono));
            r.DrawText(row.chords, { right - w, box.t, right, box.b },
                       selected ? th.rowSelectedText : th.textDim, FontRole::Mono,
                       TextAlign::Right);
            right -= w + 12.0f;
        }

        // Bookmark or open tab. Without it the same folder can appear twice with
        // nothing to say why - and it is the tab rows that behave differently on
        // Enter, so the difference has to be visible before it is pressed.
        if (box.w() > 360.0f && right - box.l - kindCol > 140.0f) {
            r.DrawText(row.kindLabel, { right - kindCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.6f), FontRole::UiSmall,
                       TextAlign::Right);
            right -= kindCol + 12.0f;
        }

        const float nameLeft = box.l + 10.0f;
        const float nameW = std::min(r.MeasureText(row.name, FontRole::Ui) + 2.0f,
                                     std::max(0.0f, (right - nameLeft) * 0.6f));
        r.DrawText(row.name, { nameLeft, box.t, nameLeft + nameW, box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        // Only what the name left over, and gone entirely when that is too little
        // to read - two strings sharing one rectangle overlap, they do not shrink.
        // Right-aligned so a long path shows its tail, which is the half that says
        // which of two same-named folders this is.
        const RectF pathBox = { nameLeft + nameW + 10.0f, box.t, right, box.b };
        if (pathBox.w() >= 60.0f) {
            r.DrawText(row.path, pathBox, th.textDim.alpha(selected ? 0.9f : 0.7f),
                       FontRole::UiSmall, TextAlign::Right);
        }

        Add(box, Hit::PlaceRow, i);
    }
    r.PopClip();

    PaintPickerScrollbar(r, body, static_cast<int>(rows.size()), frame.pageRows, first);
}

// Clicks while the bookmark list is up. Nothing behind it is reachable, the same
// as the other two overlays.
bool AppUi::HandlePlaceClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && region->kind == Hit::PlaceRow) {
        app_.placePicker().SelectRow(region->index);
        // One click goes, unlike the shortcut editor's rows: there is nothing here
        // for a first click to disambiguate, and someone who pressed a bookmark
        // has already decided. Ctrl reads as it does in the listing - a new tab.
        if (e.button == 0) app_.ChoosePlace((e.mods & kModCtrl) != 0);
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::PlacePanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowPlaces);
    return true;
}

// The command palette.
//
// The bookmark list's sibling, drawn in the same frame (PaintPickerFrame): the
// field, the fixed height and the count all come from there. It exists because a
// key that has not been learned - or that a background app has taken - leaves a
// command unreachable, and the F1 sheet only reads them out.
//
// A row holds four things: what the command is called, the name it has in
// keys.ini, which group it belongs to, and the chords that also run it. All four
// are what the filter matches, which is the reason all four are on the row -
// filtering on something the screen never shows teaches nobody it can be typed.
void AppUi::PaintCommandPalette(Renderer& r, const RectF& area) {
    const Theme& th = app_.theme();
    const Strings& str = app_.strings();
    CommandPalette& palette = app_.commandPalette();

    r.FillRect(area, th.overlayScrim);

    const std::vector<CommandPalette::Row>& rows = palette.rows();
    const float rowH = th.rowHeight;

    PickerChrome chrome;
    chrome.title = str.Get("ui.command_palette_title");
    chrome.count = str.Format(
        "ui.command_palette_count",
        { std::to_string(rows.size()) + " / " + std::to_string(palette.total()) });
    chrome.field = &palette.field();
    // The ">" is part of the text, so "nothing typed" is one byte long here.
    chrome.prefixLen = CommandPalette::kPrefix.size();
    chrome.placeholder = str.Get("ui.command_palette_search_hint");
    chrome.hint = str.Get("ui.command_palette_hint");
    const PickerFrame frame = PaintPickerFrame(r, area, chrome, Hit::PalettePanel);
    const RectF body = frame.body;
    // Told every frame: the panel is sized from the window, so the number of rows
    // a PageDown should cover is only known here.
    palette.SetPageRows(frame.pageRows);

    if (rows.empty()) {
        r.DrawText(str.Get("ui.command_palette_empty"), body.inset(10.0f, 8.0f),
                   th.textDim.alpha(0.7f), FontRole::Ui, TextAlign::Left);
        return;
    }

    const int first = palette.scroll();
    const int last = std::min(static_cast<int>(rows.size()) - 1, first + frame.pageRows - 1);

    // The three right-hand columns are measured from the rows about to be drawn,
    // the way the F1 sheet measures its chord column: a fixed width either cuts
    // the longest answer in half or leaves a gap on every other row. Measured over
    // the visible rows only, so scrolling can settle the columns tighter.
    float chordCol = 0.0f;
    float nameCol = 0.0f;
    float groupCol = 0.0f;
    const std::string unbound = str.Get("ui.command_palette_unbound");
    for (int i = first; i <= last; ++i) {
        const CommandPalette::Row& row = rows[i];
        chordCol = std::max(chordCol, row.chords.empty()
                                          ? r.MeasureText(unbound, FontRole::UiSmall)
                                          : r.MeasureText(row.chords, FontRole::Mono));
        nameCol = std::max(nameCol, r.MeasureText(row.name, FontRole::Mono));
        groupCol = std::max(groupCol, r.MeasureText(row.group, FontRole::UiSmall));
    }
    chordCol = std::min(chordCol, 180.0f);
    nameCol = std::min(nameCol, 210.0f);
    groupCol = std::min(groupCol, 130.0f);

    r.PushClip(body);
    for (int i = first; i <= last; ++i) {
        const CommandPalette::Row& row = rows[i];
        const float y = body.t + 3.0f + static_cast<float>(i - first) * rowH;
        const RectF box = { body.l + 4.0f, y, body.r - 6.0f, y + rowH };

        const bool selected = (i == palette.cursor());
        if (selected) r.FillRoundRect(box, 4.0f, th.rowSelected);
        // PointerOver, not Hovered: this panel is the overlay Hovered() blocks.
        else if (PointerOver(box)) r.FillRoundRect(box, 4.0f, th.rowHover);

        const float labelLeft = box.l + 10.0f;
        // What is left for the label decides what the row can afford. Columns are
        // dropped, never stacked - two strings sharing one rectangle overlap, they
        // do not shrink. The order they go in is "furthest from why the row was
        // read": the group repeats down the whole list, the keys.ini name is a
        // second spelling of a label that is already there, and the chords - the
        // answer to "what runs this, and does anything?" - are the last to go.
        float right = box.r - 10.0f;
        const auto affords = [&](float column) { return right - labelLeft - column > 120.0f; };

        if (affords(chordCol)) {
            const bool bound = !row.chords.empty();
            const std::string& text = bound ? row.chords : unbound;
            const FontRole font = bound ? FontRole::Mono : FontRole::UiSmall;
            r.DrawText(text, { right - chordCol, box.t, right, box.b },
                       selected ? th.rowSelectedText : th.textDim.alpha(bound ? 1.0f : 0.6f), font,
                       TextAlign::Right);
            right -= chordCol + 14.0f;
        }
        if (affords(groupCol)) {
            r.DrawText(row.group, { right - groupCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.7f), FontRole::UiSmall,
                       TextAlign::Right);
            right -= groupCol + 14.0f;
        }
        if (affords(nameCol)) {
            // The name from keys.ini, in the mono font the chords use: it is a
            // spelling to be copied into a file, not prose. Dim, because the label
            // beside it is what the row is called.
            r.DrawText(row.name, { right - nameCol, box.t, right, box.b },
                       th.textDim.alpha(selected ? 0.9f : 0.8f), FontRole::Mono,
                       TextAlign::Right);
            right -= nameCol + 14.0f;
        }

        r.DrawText(row.label, { labelLeft, box.t, std::max(labelLeft + 1.0f, right), box.b },
                   selected ? th.rowSelectedText : th.text, FontRole::Ui, TextAlign::Left);

        Add(box, Hit::PaletteRow, i);
    }
    r.PopClip();

    PaintPickerScrollbar(r, body, static_cast<int>(rows.size()), frame.pageRows, first);
}

// Clicks while the palette is up. Nothing behind it is reachable, the same as the
// other three overlays.
bool AppUi::HandlePaletteClick(const MouseEvent& e) {
    const Region* region = Pick(e.x, e.y);
    if (region && region->kind == Hit::PaletteRow) {
        app_.commandPalette().SelectRow(region->index);
        // One click runs it, the way a bookmark row goes on one click: there is
        // nothing here for a first click to disambiguate, and someone who pressed
        // a command has already decided.
        if (e.button == 0) app_.RunPaletteCommand();
        app_.host().Invalidate();
        return true;
    }
    if (region && region->kind == Hit::PalettePanel) {
        app_.host().Invalidate();
        return true;
    }
    // Outside the panel: same as pressing Escape.
    app_.Execute(Cmd::ShowCommandPalette);
    return true;
}

}  // namespace kite::ui
