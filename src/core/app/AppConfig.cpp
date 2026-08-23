// Reading and writing everything Kite keeps between runs: settings.ini,
// keys.ini, bookmarks.ini, sessions.ini - plus the settings screen's view of
// those same values.
//
// Split out of App.cpp for size alone. Keeping it together means the file
// format has one place to be read from and one to be written to, which is what
// stops a value from being saved under a name nothing loads.

#include <algorithm>

#include "core/app/App.h"
#include "core/base/PathUtil.h"
#include "core/base/Platform.h"

namespace kite {
namespace {

const char* SortKeyName(SortKey k) {
    switch (k) {
        case SortKey::Ext: return "ext";
        case SortKey::Size: return "size";
        case SortKey::Date: return "date";
        default: return "name";
    }
}

SortKey SortKeyFromName(const std::string& s) {
    if (s == "ext") return SortKey::Ext;
    if (s == "size") return SortKey::Size;
    if (s == "date") return SortKey::Date;
    return SortKey::Name;
}

// Text-size limits. The floor is where the row height stops leaving room for an
// icon, the ceiling where a pane stops holding enough rows to be a listing.
// The settings screen offers the same range in the same 0.1 steps; widening one
// side alone would leave sizes the keys can reach and the screen cannot.
constexpr float kFontScaleMin = 0.7f;
constexpr float kFontScaleMax = 2.0f;

const char* NewTabPositionName(NewTabPosition p) {
    return p == NewTabPosition::AfterCurrent ? "after_current" : "end";
}

NewTabPosition NewTabPositionFromName(const std::string& s) {
    return s == "after_current" ? NewTabPosition::AfterCurrent : NewTabPosition::End;
}

const char* TabBarPositionName(TabBarPosition p) {
    return p == TabBarPosition::Left ? "left" : "top";
}

TabBarPosition TabBarPositionFromName(const std::string& s) {
    return s == "left" ? TabBarPosition::Left : TabBarPosition::Top;
}

// Section in settings.ini holding one such order, or nullptr for a section
// whose order is its own data (bookmarks.ini is the bookmark order).
const char* SidebarOrderSection(SidebarSection section) {
    switch (section) {
        case SidebarSection::QuickAccess: return "sidebar.quick_access";
        case SidebarSection::Drives: return "sidebar.drives";
        default: return nullptr;
    }
}

void ReadOrder(const Ini& ini, const char* section, std::vector<std::string>& out) {
    out.clear();
    const Ini::Section* sec = ini.Find(section);
    if (!sec) return;
    for (const Ini::Entry& e : sec->entries) out.push_back(e.value);
}

// The name a section goes by in settings.ini. Stable across releases: it is
// what both the collapse flag and the section order are written in terms of.
const char* SidebarSectionName(SidebarSection section) {
    switch (section) {
        case SidebarSection::QuickAccess: return "quick_access";
        case SidebarSection::Bookmarks: return "bookmarks";
        case SidebarSection::Drives: return "drives";
        default: return "";
    }
}

SidebarSection SidebarSectionFromName(const std::string& name) {
    for (size_t i = 0; i < static_cast<size_t>(SidebarSection::Count); ++i) {
        const SidebarSection section = static_cast<SidebarSection>(i);
        if (name == SidebarSectionName(section)) return section;
    }
    return SidebarSection::Count;
}

std::string SidebarCollapseKey(SidebarSection section) {
    return std::string("sidebar_collapse_") + SidebarSectionName(section);
}

}  // namespace

std::string App::ConfigPath(const char* file) const {
    return path::Join(fs_.ConfigDir(), file);
}

// Kite's own writes are the only ones that can fail without the user having
// asked for anything, so they are the only ones that have to announce
// themselves. The message names the full path rather than the file: what went
// wrong is almost always the folder (extracted into Program Files, run from
// read-only media), and the file name alone does not say which folder it is.
bool App::WriteConfigFile(const char* file, std::string_view data) {
    const std::string path = ConfigPath(file);
    if (plat::WriteTextFile(path, data)) return true;
    SetStatus(strings_.Format("ui.config_write_failed", { path }));
    return false;
}

// The order the three sections stand in. Anything the file does not name is
// appended in the built-in order: a name that was mistyped, or a section added
// in a later version, must not make the section disappear from the sidebar.
void App::LoadSidebarSections() {
    sidebarSections_.clear();
    if (const Ini::Section* sec = settings_.Find("sidebar")) {
        for (const Ini::Entry& e : sec->entries) {
            const SidebarSection section = SidebarSectionFromName(e.value);
            if (section == SidebarSection::Count) continue;
            if (std::find(sidebarSections_.begin(), sidebarSections_.end(), section) !=
                sidebarSections_.end()) {
                continue;  // named twice; the first mention wins
            }
            sidebarSections_.push_back(section);
        }
    }
    for (size_t i = 0; i < static_cast<size_t>(SidebarSection::Count); ++i) {
        const SidebarSection section = static_cast<SidebarSection>(i);
        if (std::find(sidebarSections_.begin(), sidebarSections_.end(), section) ==
            sidebarSections_.end()) {
            sidebarSections_.push_back(section);
        }
    }
}

void App::LoadConfig() {
    // The folder itself failing is worth saying on its own: every write after
    // this one will fail too, and at that point nothing at all is being kept.
    if (!plat::EnsureDirectory(fs_.ConfigDir())) {
        SetStatus(strings_.Format("ui.config_write_failed", { fs_.ConfigDir() }));
    }

    std::string text;
    settings_ = Ini();
    if (plat::ReadTextFile(ConfigPath("settings.ini"), text)) settings_.Parse(text);

    darkTheme_ = settings_.GetStr("ui", "theme", "dark") != "light";
    fontScale_ = std::clamp(settings_.GetFloat("ui", "font_scale", 1.0f), kFontScaleMin,
                            kFontScaleMax);
    ApplyTheme();

    language_ = settings_.GetStr("ui", "language", "auto");
    LoadLanguage();

    sidebarVisible_ = settings_.GetBool("ui", "sidebar", true);
    // 既定は現行動作の「末尾」。ブラウザに合わせて隣に挿す人と、開いた順に並べて
    // おきたい人で分かれるので、どちらかを正解にせず設定にしてある。
    newTabPosition_ = NewTabPositionFromName(settings_.GetStr("ui", "new_tab_position", "end"));
    tabBarPosition_ = TabBarPositionFromName(settings_.GetStr("ui", "tab_bar_position", "top"));
    ReadOrder(settings_, SidebarOrderSection(SidebarSection::QuickAccess), quickAccessOrder_);
    ReadOrder(settings_, SidebarOrderSection(SidebarSection::Drives), driveOrder_);
    for (size_t i = 0; i < static_cast<size_t>(SidebarSection::Count); ++i) {
        sidebarCollapsed_[i] =
            settings_.GetBool("ui", SidebarCollapseKey(static_cast<SidebarSection>(i)), false);
    }
    LoadSidebarSections();
    // The escape hatch for the one thing shell icons cost: they are what pulls
    // third-party overlay handlers into the picture at all. Off, Kite draws its
    // own vector glyphs and never asks the shell about a file.
    shellIcons_ = settings_.GetBool("ui", "shell_icons", true);
    // On by default. Nothing is lost by it: the shell's own context menu still
    // carries whatever extractor is installed, and ".." leads back out - while
    // off by default would mean nobody learns the feature exists.
    openArchives_ = settings_.GetBool("ui", "open_archives", true);

    defaultView_.showHidden = settings_.GetBool("view", "show_hidden", false);
    defaultView_.dirsFirst = settings_.GetBool("view", "dirs_first", true);
    defaultView_.sortDesc = settings_.GetBool("view", "sort_desc", false);
    defaultView_.sort = SortKeyFromName(settings_.GetStr("view", "sort", "name"));

    placement_.x = settings_.GetInt("window", "x", -1);
    placement_.y = settings_.GetInt("window", "y", -1);
    placement_.w = settings_.GetInt("window", "w", 1180);
    placement_.h = settings_.GetInt("window", "h", 720);
    placement_.maximized = settings_.GetBool("window", "maximized", false);
    // 2 枚目は大きさだけ受け継ぎ、位置は OS に選ばせる。保存された座標をそのまま
    // 使うと、開いた瞬間に元のウィンドウとぴったり重なって増えたことが分からない。
    if (standalone_) {
        placement_.x = -1;
        placement_.y = -1;
    }

    keymap_.LoadDefaults();
    Ini keysIni;
    if (plat::ReadTextFile(ConfigPath("keys.ini"), text)) {
        keysIni.Parse(text);
        std::vector<std::string> warnings;
        keymap_.ApplyIni(keysIni, &warnings);
        if (!warnings.empty()) SetStatus(warnings.front());
    } else {
        WriteKeysFile();
    }
    keyEditor_.Close();
    // The rows hold positions in the bookmark list that is about to be replaced.
    placePicker_.Close();
    // And these hold labels and chords from the language and keymap being replaced
    // right now - a palette left open would be offering the previous ones.
    commandPalette_.Close();

    workspace_.bookmarks.clear();
    Ini bookmarksIni;
    if (plat::ReadTextFile(ConfigPath("bookmarks.ini"), text)) {
        bookmarksIni.Parse(text);
        if (const Ini::Section* sec = bookmarksIni.Find("bookmarks")) {
            for (const Ini::Entry& e : sec->entries) {
                workspace_.bookmarks.push_back({ e.key, e.value });
            }
        }
    }
}

// The language is always loaded in the same two steps: the built-in table, then
// the user's lang.<code>.ini on top. Anything that reloads the table without the
// second step - the language toggle used to - silently drops every line the user
// translated themselves.
void App::LoadLanguage() {
    std::string code = language_;
    if (code == "auto" || code.empty()) code = plat::PreferredLanguage();
    strings_.Load(code);

    // Optional user translation file, e.g. lang.ja.ini - lets a new locale ship
    // without touching the binary.
    std::string text;
    const std::string langFile = path::Join(fs_.ConfigDir(), "lang." + strings_.code() + ".ini");
    if (plat::ReadTextFile(langFile, text)) {
        Ini langIni;
        langIni.Parse(text);
        strings_.ApplyOverrides(langIni);
    }
}

// The theme is always rebuilt from the same three steps, in this order: the
// built-in defaults, what settings.ini says about them, then the text size the
// user asked for on top. Anything that skips a step - the theme toggle used to -
// silently drops whichever of the three came later.
void App::ApplyTheme() {
    theme_ = darkTheme_ ? Theme::Dark() : Theme::Light();
    theme_.ApplyIni(settings_);
    theme_.Scale(fontScale_);
}

void App::SetFontScale(float scale) {
    const float wanted = std::clamp(scale, kFontScaleMin, kFontScaleMax);
    if (wanted == fontScale_) {
        // Already at the limit. Say the size rather than letting the key look dead.
        SetStatus(strings_.Format("ui.font_scale",
                                  { std::to_string(static_cast<int>(fontScale_ * 100.0f + 0.5f)) }));
        return;
    }
    fontScale_ = wanted;
    ApplyTheme();
    SetStatus(strings_.Format("ui.font_scale",
                              { std::to_string(static_cast<int>(fontScale_ * 100.0f + 0.5f)) }));
    dirty_ = true;
    host_.Invalidate();
}

bool App::sidebarCollapsed(SidebarSection section) const {
    if (section == SidebarSection::Count) return false;
    return sidebarCollapsed_[static_cast<size_t>(section)];
}

void App::ToggleSidebarSection(SidebarSection section) {
    if (section == SidebarSection::Count) return;
    bool& collapsed = sidebarCollapsed_[static_cast<size_t>(section)];
    collapsed = !collapsed;
    dirty_ = true;
    host_.Invalidate();
}

// Written out the moment anything changes rather than when the screen closes: a
// binding the user just watched take effect must survive the app being killed.
// Only a write that landed earns the confirmation on the way out; otherwise the
// failure message stands.
void App::SaveKeysIfChanged() {
    if (!keyEditor_.dirty()) return;
    keysChanged_ = WriteKeysFile();
    keyEditor_.ClearDirty();
}

void App::RemoveKeyBinding(int index) {
    keyEditor_.RemoveChord(index, keymap_, strings_);
    SaveKeysIfChanged();
    host_.Invalidate();
}

bool App::WriteKeysFile() {
    std::string out =
        "# Kite key bindings.\n"
        "#\n"
        "# Syntax:  <command> = <chord>\n"
        "#   Chords combine Ctrl / Shift / Alt with a key name, e.g. Ctrl+Shift+T.\n"
        "#   Repeat a command on several lines to give it several chords.\n"
        "#   Use \"<command> = none\" to leave a command with no key at all.\n"
        "#   The lines here are the whole answer for the commands they name: a\n"
        "#   command listed once has exactly that one chord, built-in default or\n"
        "#   not. Delete a command's lines entirely to get its default back.\n"
        "#\n"
        "# Written from the bindings that were active at the time. Edit it here and\n"
        "# reload with Ctrl+Alt+C, or edit it on screen with Ctrl+F1 - which rewrites\n"
        "# this file, comments and all.\n\n";
    out += keymap_.ToIni().Serialize();
    return WriteConfigFile("keys.ini", out);
}

// 設定画面に見せる現在値。実体はここにある個々のメンバで、設定画面が持つのは
// 「選択肢の何番目か」だけ。
SettingsValues App::CollectSettings() const {
    SettingsValues v;
    v.Set(SettingId::Theme, darkTheme_ ? 0 : 1);
    v.Set(SettingId::Language, LanguageIndex(language_));
    v.Set(SettingId::FontScale, FontScaleIndex(fontScale_));
    v.Set(SettingId::Sidebar, sidebarVisible_ ? 1 : 0);
    v.Set(SettingId::ShellIcons, shellIcons_ ? 1 : 0);
    v.Set(SettingId::OpenArchives, openArchives_ ? 1 : 0);
    v.Set(SettingId::TabBarPos, tabBarPosition_ == TabBarPosition::Left ? 1 : 0);
    v.Set(SettingId::NewTabPos, newTabPosition_ == NewTabPosition::AfterCurrent ? 1 : 0);
    v.Set(SettingId::NewTabHidden, defaultView_.showHidden ? 1 : 0);
    v.Set(SettingId::NewTabDirsFirst, defaultView_.dirsFirst ? 1 : 0);
    return v;
}

// 変わった 1 項目だけを反映する。全項目を書き戻すと、設定画面が選択肢を持たない値
// （lang.fr.ini を置いた人の language = fr）まで先頭の選択肢で上書きされる。
void App::ApplySetting(SettingId id, const SettingsValues& values) {
    const int index = values.Get(id);
    switch (id) {
        case SettingId::Theme:
            darkTheme_ = (index == 0);
            ApplyTheme();
            break;
        case SettingId::Language:
            language_ = LanguageCode(index);
            LoadLanguage();
            break;
        case SettingId::FontScale:
            // SetFontScale はステータス行に倍率を出すが、ここでは画面に値が出て
            // いるので通さない。テーマの組み直しだけを借りる。
            fontScale_ = std::clamp(FontScaleValue(index), kFontScaleMin, kFontScaleMax);
            ApplyTheme();
            break;
        case SettingId::Sidebar:
            sidebarVisible_ = (index != 0);
            break;
        case SettingId::ShellIcons:
            shellIcons_ = (index != 0);
            break;
        case SettingId::OpenArchives:
            openArchives_ = (index != 0);
            break;
        case SettingId::TabBarPos:
            tabBarPosition_ = (index != 0) ? TabBarPosition::Left : TabBarPosition::Top;
            break;
        case SettingId::NewTabPos:
            newTabPosition_ = (index != 0) ? NewTabPosition::AfterCurrent : NewTabPosition::End;
            break;
        case SettingId::NewTabHidden:
            defaultView_.showHidden = (index != 0);
            break;
        case SettingId::NewTabDirsFirst:
            defaultView_.dirsFirst = (index != 0);
            break;
        default:
            return;
    }
    dirty_ = true;
    host_.Invalidate();
}

void App::ApplyPendingSetting() {
    const SettingId id = settingsEditor_.changed();
    if (id == SettingId::Count) return;
    ApplySetting(id, settingsEditor_.values());
    settingsEditor_.ClearChanged();
    // 言語を変えると自分自身のラベルも変わる。
    settingsEditor_.Rebuild(strings_);

    // キーの設定と同じ扱いで、その場で書き出す ─ 目の前で効いた変更が、次に落ちた
    // ときに黙って消えるほうが実害が大きい。単独ウィンドウは何も書かない
    // （SaveAll と同じ理由。書けば後から閉じたほうが本体の設定を古い内容で潰す）。
    if (!standalone_) SaveSettings();
}

int App::NewTabAt(const Pane& pane) const {
    return newTabPosition_ == NewTabPosition::AfterCurrent ? pane.active + 1 : -1;
}

void App::CloseKeyEditor() {
    keyEditor_.Close();
    // Every change went to disk as it was made; this only confirms it once, on
    // the way out, instead of after every keystroke.
    if (keysChanged_) {
        SetStatus(strings_.Get("ui.key_settings_saved"));
        keysChanged_ = false;
    }
}

bool App::SaveSettings() {
    settings_.Set("ui", "theme", darkTheme_ ? "dark" : "light");
    settings_.Set("ui", "language", language_);
    settings_.SetBool("ui", "sidebar", sidebarVisible_);
    for (size_t i = 0; i < static_cast<size_t>(SidebarSection::Count); ++i) {
        settings_.SetBool("ui", SidebarCollapseKey(static_cast<SidebarSection>(i)),
                          sidebarCollapsed_[i]);
    }
    settings_.ClearSection("sidebar");
    for (SidebarSection section : sidebarSections_) {
        settings_.Append("sidebar", "section", SidebarSectionName(section));
    }
    settings_.SetFloat("ui", "font_scale", fontScale_);
    settings_.SetBool("ui", "shell_icons", shellIcons_);
    settings_.SetBool("ui", "open_archives", openArchives_);
    settings_.Set("ui", "new_tab_position", NewTabPositionName(newTabPosition_));
    settings_.Set("ui", "tab_bar_position", TabBarPositionName(tabBarPosition_));

    // Rewritten whole rather than merged: a folder that has since disappeared
    // would otherwise sit in the file forever, holding a slot nothing fills.
    // Left alone entirely until something has actually been dragged, so the
    // file does not grow an empty section for everyone else.
    auto saveOrder = [&](SidebarSection section, const std::vector<std::string>& order) {
        const char* name = SidebarOrderSection(section);
        if (order.empty() && !settings_.Find(name)) return;
        settings_.ClearSection(name);
        for (const std::string& path : order) settings_.Append(name, "item", path);
    };
    saveOrder(SidebarSection::QuickAccess, quickAccessOrder_);
    saveOrder(SidebarSection::Drives, driveOrder_);

    settings_.SetBool("view", "show_hidden", defaultView_.showHidden);
    settings_.SetBool("view", "dirs_first", defaultView_.dirsFirst);
    settings_.SetBool("view", "sort_desc", defaultView_.sortDesc);
    settings_.Set("view", "sort", SortKeyName(defaultView_.sort));

    settings_.SetInt("window", "x", placement_.x);
    settings_.SetInt("window", "y", placement_.y);
    settings_.SetInt("window", "w", placement_.w);
    settings_.SetInt("window", "h", placement_.h);
    settings_.SetBool("window", "maximized", placement_.maximized);

    const bool settingsOk = WriteConfigFile("settings.ini", settings_.Serialize());

    Ini bookmarksIni;
    for (const Bookmark& b : workspace_.bookmarks) {
        bookmarksIni.Append("bookmarks", b.name, b.path);
    }
    // Attempted even if the settings write just failed: the two files fail for
    // the same reason often enough, but not always - a folder can hold one file
    // open and not the other, and skipping the second would lose bookmarks that
    // could still have been written.
    const bool bookmarksOk = WriteConfigFile("bookmarks.ini", bookmarksIni.Serialize());
    return settingsOk && bookmarksOk;
}

void App::LoadWorkspace(const std::vector<std::string>& startPaths) {
    std::string text;
    Ini ws;
    // 単独ウィンドウは保存されたセッションを読まない。読めば「新しいウィンドウ」に
    // 元の窓の全セッションが複製され、頼んだフォルダはその何枚目かのタブになる。
    if (!standalone_ && plat::ReadTextFile(ConfigPath("sessions.ini"), text)) ws.Parse(text);

    for (int i = 0;; ++i) {
        const std::string sec = "session." + std::to_string(i);
        if (!ws.Find(sec)) break;
        const std::string name = ws.GetStr(sec, "name", "Session " + std::to_string(i + 1));
        const std::string layout = ws.GetStr(sec, "layout");
        std::unique_ptr<Session> s = Session::Deserialize(name, layout);
        if (s) workspace_.sessions.push_back(std::move(s));
    }

    // 単独ウィンドウでは開始位置そのものが最初のタブになる。ここで home を開くと、
    // 頼まれていない場所のタブが必ず 1 枚余る。
    size_t firstExtra = 0;
    if (workspace_.sessions.empty()) {
        std::string start = fs_.HomeDir();
        if (standalone_ && !startPaths.empty()) {
            start = path::Normalize(startPaths.front());
            firstExtra = 1;
        }
        workspace_.AddSession(strings_.Format("ui.new_session", { "1" }), start);
    }
    workspace_.active = std::clamp(ws.GetInt("workspace", "active", 0), 0,
                                   static_cast<int>(workspace_.sessions.size()) - 1);

    // Seed every tab's view state from the saved defaults.
    for (const std::unique_ptr<Session>& s : workspace_.sessions) {
        for (Pane* p : s->Panes()) {
            for (std::unique_ptr<Tab>& t : p->tabs) t->view = defaultView_;
        }
    }

    // Command-line paths open as extra tabs in the focused pane.
    if (firstExtra < startPaths.size()) {
        if (Pane* p = workspace_.focusedPane()) {
            for (size_t i = firstExtra; i < startPaths.size(); ++i) {
                Tab* t = p->AddTab(path::Normalize(startPaths[i]));
                t->view = defaultView_;
            }
        }
    }
}

bool App::SaveWorkspaceFile() {
    Ini ws;
    ws.SetInt("workspace", "active", workspace_.active);
    for (size_t i = 0; i < workspace_.sessions.size(); ++i) {
        const std::string sec = "session." + std::to_string(i);
        ws.Set(sec, "name", workspace_.sessions[i]->name);
        ws.Set(sec, "layout", workspace_.sessions[i]->Serialize());
    }
    return WriteConfigFile("sessions.ini", ws.Serialize());
}

bool App::SaveAll() {
    // 単独ウィンドウは何も書かない。設定もセッションも起動時の写しでしかないので、
    // 後から閉じたほうが本体の変更を古い内容で上書きしてしまう。
    if (standalone_) return true;
    const bool settingsOk = SaveSettings();
    const bool workspaceOk = SaveWorkspaceFile();
    // 書けなかったものが残っているうちは「保存済み」にしない。
    dirty_ = !(settingsOk && workspaceOk);
    return settingsOk && workspaceOk;
}

}  // namespace kite
