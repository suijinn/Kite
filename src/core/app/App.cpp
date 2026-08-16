#include "core/app/App.h"

#include <algorithm>

#include "core/base/Format.h"
#include "core/base/PathUtil.h"
#include "core/base/Platform.h"
#include "core/base/Utf8.h"
#include "core/base/Version.h"

namespace kite {
namespace {

constexpr uint64_t kStatusDurationMs = 4000;

// How far into the row the keyboard-invoked shell menu is anchored. The menu
// opens down and to the right of this point, so a small indent puts its corner
// under the item's icon rather than out on the pane border.
constexpr float kMenuAnchorIndent = 8.0f;

// Clipboard text on its way into a one-line field.
//
// Only the first line survives: the field holds one path, and a multi-line
// paste would otherwise arrive as one run-on string with the newlines invisible.
// Surrounding quotes go too - Explorer's "Copy as path" wraps its answer in
// them, and pasting that verbatim gives a path no filesystem has ever heard of.
std::string ClipboardToField(std::string_view text) {
    size_t begin = 0;
    size_t end = text.find_first_of("\r\n");
    if (end == std::string_view::npos) end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    if (end - begin >= 2 && text[begin] == '"' && text[end - 1] == '"') {
        ++begin;
        --end;
    }
    std::string out(text.substr(begin, end - begin));
    // Anything left below space would draw as a blank the caret walks through.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](char c) { return static_cast<unsigned char>(c) < 0x20; }),
              out.end());
    return out;
}

// Whether each source already has a namesake in the destination, in the order
// the sources were given. Taken before a transfer so the same question can be
// asked again afterwards: the shell resolves a collision either by renaming its
// copy or by overwriting, and neither outcome is visible from the return value.
std::vector<bool> DestinationsExist(fs::IFileSystem& fs, const std::vector<std::string>& sources,
                                    const std::string& destDir) {
    std::vector<bool> out;
    out.reserve(sources.size());
    for (const std::string& s : sources) {
        out.push_back(fs.Exists(path::Join(destDir, path::FileName(s))));
    }
    return out;
}

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
constexpr float kFontScaleStep = 0.1f;

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

// Move one element, taking `to` as the index it should end up at once the
// element has been lifted out - the same convention Pane::ReorderTab uses.
template <typename T>
bool MoveInVector(std::vector<T>& items, int from, int to) {
    const int count = static_cast<int>(items.size());
    if (from < 0 || from >= count) return false;
    to = std::clamp(to, 0, count - 1);
    if (from == to) return false;

    T moved = std::move(items[static_cast<size_t>(from)]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
    return true;
}

std::vector<std::string> PathsOf(const std::vector<fs::Root>& roots) {
    std::vector<std::string> out;
    out.reserve(roots.size());
    for (const fs::Root& r : roots) out.push_back(r.path);
    return out;
}

// Put `roots` back into the order the user dragged them into. Anything the
// saved order does not name - a drive plugged in since, a cloud folder that
// appeared - keeps its enumeration order and follows behind, rather than
// landing in an arbitrary spot in the middle.
void ApplySavedOrder(std::vector<fs::Root>& roots, const std::vector<std::string>& order) {
    if (order.empty() || roots.empty()) return;

    std::vector<fs::Root> sorted;
    sorted.reserve(roots.size());
    for (const std::string& path : order) {
        auto it = std::find_if(roots.begin(), roots.end(), [&](const fs::Root& r) {
            return utf8::EqualsIgnoreCaseAscii(r.path, path);
        });
        if (it == roots.end()) continue;  // no longer present; its slot just closes up
        sorted.push_back(std::move(*it));
        roots.erase(it);
    }
    for (fs::Root& r : roots) sorted.push_back(std::move(r));
    roots = std::move(sorted);
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

App::App(fs::IFileSystem& filesystem, IShellIntegration& shell, IHost& host,
         fs::IDirectoryWatcher* watcher)
    : fs_(filesystem), shell_(shell), host_(host), watcher_(watcher) {
    theme_ = Theme::Dark();
}

App::~App() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::Init(const std::vector<std::string>& startPaths) {
    LoadConfig();
    loader_ = std::make_unique<fs::DirectoryLoader>(fs_, host_, 2);
    RefreshRoots();
    LoadWorkspace(startPaths);
    EnsureVisibleTabsLoaded();
    // Before any command has run, so the window does not sit there captioned
    // with the bare class name until the user touches something.
    UpdateTitle();
    return true;
}

void App::Shutdown() {
    SaveAll();
    loader_.reset();  // joins workers before anything else is torn down
}

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

uint32_t App::IconFor(const std::string& path) {
    if (!icons_ || !shellIcons_) return 0;
    return icons_->IconFor(path);
}

void App::RefreshRoots() {
    roots_ = fs_.Roots();
    quickAccess_ = fs_.QuickAccess();
    // Both lists come straight from the OS, in the OS's order. Any dragging the
    // user did lives in the saved order and has to be laid back over them here -
    // this runs again whenever the drive list changes, not just at start-up.
    ApplySavedOrder(quickAccess_, quickAccessOrder_);
    ApplySavedOrder(roots_, driveOrder_);
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

bool App::MoveSidebarSection(int from, int to) {
    if (!MoveInVector(sidebarSections_, from, to)) return false;
    dirty_ = true;
    host_.Invalidate();
    return true;
}

int App::SidebarItemCount(SidebarSection section) const {
    switch (section) {
        case SidebarSection::QuickAccess: return static_cast<int>(quickAccess_.size());
        case SidebarSection::Bookmarks: return static_cast<int>(workspace_.bookmarks.size());
        case SidebarSection::Drives: return static_cast<int>(roots_.size());
        default: return 0;
    }
}

bool App::MoveSidebarItem(SidebarSection section, int from, int to) {
    bool moved = false;
    switch (section) {
        case SidebarSection::QuickAccess:
            moved = MoveInVector(quickAccess_, from, to);
            if (moved) quickAccessOrder_ = PathsOf(quickAccess_);
            break;
        case SidebarSection::Bookmarks:
            // No separate order to record: bookmarks.ini is written in this
            // order, and Alt+Shift+1..8 counts through the same list.
            moved = MoveInVector(workspace_.bookmarks, from, to);
            break;
        case SidebarSection::Drives:
            moved = MoveInVector(roots_, from, to);
            if (moved) driveOrder_ = PathsOf(roots_);
            break;
        default:
            return false;
    }
    if (!moved) return false;
    dirty_ = true;
    host_.Invalidate();
    return true;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void App::RequestLoad(Tab& tab, bool force) {
    if (!loader_) return;
    if (!force && (tab.loaded || tab.loadToken != 0)) return;
    tab.loadToken = loader_->Request(tab.path);
}

void App::EnsureVisibleTabsLoaded() {
    Session* s = workspace_.activeSession();
    if (!s) return;
    for (Pane* p : s->Panes()) {
        if (Tab* t = p->activeTab()) RequestLoad(*t);
    }
    SyncWatches();
}

void App::SyncWatches() {
    if (!watcher_) return;

    // Only what is on screen gets a watch; a background tab is re-listed when
    // it comes forward instead.
    std::unordered_map<uint64_t, std::string> desired;
    std::vector<Tab*> newlyWatched;

    if (Session* s = workspace_.activeSession()) {
        for (Pane* p : s->Panes()) {
            Tab* t = p->activeTab();
            if (!t) continue;
            if (t->watchId == 0) t->watchId = NextWatchId();
            desired[t->watchId] = t->path;

            auto previous = watched_.find(t->watchId);
            if (previous == watched_.end() || previous->second != t->path) {
                newlyWatched.push_back(t);
            }
        }
    }

    for (const auto& [id, path] : watched_) {
        auto it = desired.find(id);
        if (it == desired.end() || it->second != path) watcher_->Unwatch(id);
    }

    for (Tab* t : newlyWatched) {
        watcher_->Watch(t->watchId, t->path);
        // Nothing was reporting changes for this folder until now, so anything
        // already on screen for it may be stale. A tab that has not loaded yet
        // has a request in flight and needs no second one.
        if (t->loaded) RequestLoad(*t, true);
    }

    watched_ = std::move(desired);
}

void App::PumpLoader() {
    // Filesystem notifications arrive on the same wake-up as finished listings.
    if (watcher_) {
        std::vector<fs::ChangeEvent> changes;
        watcher_->Drain(changes);
        for (const fs::ChangeEvent& change : changes) {
            for (const std::unique_ptr<Session>& s : workspace_.sessions) {
                for (Pane* p : s->Panes()) {
                    Tab* t = p->activeTab();
                    // Re-list only if the tab is still showing the folder that
                    // changed; it may have navigated away in the meantime.
                    if (t && t->watchId == change.watchId && t->path == change.path) {
                        RequestLoad(*t, true);
                    }
                }
            }
        }
    }

    if (!loader_) return;
    std::vector<fs::LoadedListing> done;
    loader_->Drain(done);
    if (done.empty()) return;

    for (fs::LoadedListing& l : done) {
        if (completeToken_ != 0 && l.token == completeToken_) {
            completeToken_ = 0;
            // Handed over even when the listing failed: a folder that does not
            // exist has no candidates, and saying so once stops the request
            // from being made again on the next keystroke.
            complete_.SetListing(l.path, l.result.entries);
            continue;
        }
        for (const std::unique_ptr<Session>& s : workspace_.sessions) {
            for (Pane* p : s->Panes()) {
                for (std::unique_ptr<Tab>& t : p->tabs) {
                    if (t->loadToken != l.token) continue;
                    t->listing = std::move(l.result);
                    t->loadToken = 0;
                    t->loaded = true;
                    t->marked.assign(t->listing.entries.size(), 0);
                    t->Rebuild();
                    // "Access denied" on a share is usually not a verdict but a
                    // question that has not been asked yet. The listing itself
                    // cannot ask it - a credential dialog raised from a worker
                    // thread would also fire while the address bar is being
                    // typed into - so name the key that does.
                    if (t.get() == workspace_.focusedTab() &&
                        t->listing.status == fs::Status::AccessDenied &&
                        !path::UncRoot(t->path).empty()) {
                        SetStatus(strings_.Format("ui.network_auth_hint",
                                                  { keymap_.ChordText(Cmd::ConnectNetwork) }));
                    }
                }
            }
        }
    }
    // The answer that just arrived may already be for the wrong folder - the
    // user kept typing while it was in flight - so ask again from here.
    RequestCompletion();
    EnsureCursorVisible();
    UpdateTitle();
    host_.Invalidate();
}

void App::UpdateTitle() {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    Session* s = workspace_.activeSession();
    // The build ends the caption rather than starting it: it is what someone
    // filing a report needs, and the folder is what everyone else reads.
    std::string title = t->path + "  \xE2\x80\x94  " + (s ? s->name : std::string("Kite")) +
                        "  \xE2\x80\x94  Kite " + version::kDisplay;
    // Only touch the caption when it actually changed: SetWindowText on every
    // cursor keystroke makes the taskbar entry flicker.
    if (title == lastTitle_) return;
    lastTitle_ = std::move(title);
    host_.SetTitle(lastTitle_);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void App::FocusPane(Pane* pane) {
    Session* s = workspace_.activeSession();
    if (!s || !pane) return;
    s->focus = pane;
    if (Tab* t = pane->activeTab()) RequestLoad(*t);
    SyncWatches();
    host_.Invalidate();
}

void App::SetWindowActive(bool active) {
    if (windowActive_ == active) return;
    windowActive_ = active;
    // The focus ring and the cursor row are drawn from this, so what is on
    // screen is now wrong. Guarded above because the platform layer gets told
    // about activation far more often than it actually changes.
    host_.Invalidate();
}

void App::NavigateFocused(const std::string& raw) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    const std::string target = path::Normalize(raw);
    if (target.empty()) return;

    if (target != t->path) {
        t->back.push_back(t->path);
        t->forward.clear();
        t->path = target;
    }
    t->filter.clear();
    t->cursor = 0;
    t->anchor = 0;
    t->scroll = 0.0f;
    t->listing.entries.clear();
    t->visible.clear();
    t->marked.clear();
    t->loaded = false;
    RequestLoad(*t, true);
    SyncWatches();
    dirty_ = true;
    host_.Invalidate();
}

void App::OpenPath(const std::string& p, bool newTab) {
    Pane* pane = workspace_.focusedPane();
    if (!pane) return;
    if (newTab) {
        Tab* t = pane->AddTab(path::Normalize(p), NewTabAt(*pane));
        t->view = defaultView_;
        RequestLoad(*t, true);
        dirty_ = true;
        host_.Invalidate();
    } else {
        NavigateFocused(p);
    }
}

void App::OpenForwardedPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    Pane* pane = workspace_.focusedPane();
    if (!pane) return;

    // Appended in the order they arrived, ignoring [ui] new_tab_position: these
    // are the second launch's command-line arguments, and someone who asked for
    // three folders side by side asked for that order. AfterCurrent would stack
    // them backwards, each one landing in front of the last.
    Tab* last = nullptr;
    for (const std::string& p : paths) {
        if (p.empty()) continue;
        last = pane->AddTab(path::Normalize(p));
        last->view = defaultView_;
        RequestLoad(*last, true);
    }
    if (!last) return;

    pane->Activate(static_cast<int>(pane->tabs.size()) - 1);
    dirty_ = true;
    SyncWatches();
    UpdateTitle();
    host_.Invalidate();
}

void App::NavigateToParent(bool newTab) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    const std::string parent = path::Parent(t->path);
    if (parent.empty()) return;
    const std::string leaving = path::FileName(t->path);
    OpenPath(parent, newTab);
    // Land the cursor on the folder we just came out of.
    if (Tab* now = workspace_.focusedTab()) now->pendingFocusName = leaving;
}

void App::ActivateEntry(int visibleIndex, bool newTab) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    if (t->IsParentRow(visibleIndex)) {
        NavigateToParent(newTab);
        return;
    }
    const fs::Entry* entry = t->EntryAt(visibleIndex);
    if (!entry) return;

    const fs::Entry& e = *entry;
    const std::string full = path::Join(t->path, e.name);
    if (e.isDir()) {
        OpenPath(full, newTab);
    } else {
        // Never read the file here: a cloud placeholder must be hydrated by the
        // shell, on the shell's terms.
        shell_.Open(full);
    }
}

void App::RefreshFocused() {
    if (Tab* t = workspace_.focusedTab()) {
        RequestLoad(*t, true);
        // Overlays show a file's state (committed, synced, locked), and every
        // caller here has just changed something - including the shell menu,
        // which is where a commit or a sync is started from. Nothing tells us
        // when a handler changes its mind, so a refresh asks again.
        if (icons_ && shellIcons_) icons_->Invalidate();
        host_.Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Drag & drop
// ---------------------------------------------------------------------------

bool App::IsValidDropTarget(const std::vector<std::string>& paths, const std::string& destDir) {
    if (destDir.empty() || paths.empty()) return false;
    const std::string dest = path::Normalize(destDir);

    for (const std::string& p : paths) {
        const std::string src = path::Normalize(p);
        if (src.empty()) return false;
        if (utf8::EqualsIgnoreCaseAscii(src, dest)) return false;  // onto itself

        // Refuse to drop a folder into its own subtree - that is the one
        // mistake here that can destroy data.
        if (dest.size() > src.size() &&
            utf8::EqualsIgnoreCaseAscii(dest.substr(0, src.size()), src) &&
            path::IsSep(dest[src.size()])) {
            return false;
        }
    }
    return true;
}

bool App::PerformDrop(const std::vector<std::string>& paths, const std::string& destDir,
                      bool move) {
    if (!IsValidDropTarget(paths, destDir)) {
        SetStatus(strings_.Get("ui.drop_invalid"));
        host_.Invalidate();
        return false;
    }

    // Moving something into the folder it already lives in is a no-op, not an
    // error; drop those silently rather than letting the shell complain.
    std::vector<std::string> sources;
    for (const std::string& p : paths) {
        if (move && utf8::EqualsIgnoreCaseAscii(path::Parent(p), destDir)) continue;
        sources.push_back(p);
    }
    if (sources.empty()) return false;

    const std::vector<bool> existedBefore = DestinationsExist(fs_, sources, destDir);

    std::string err;
    if (!fs_.CopyTo(sources, destDir, move, &err)) {
        if (!err.empty()) SetStatus(err);
        return false;
    }
    RecordTransfer(sources, destDir, existedBefore, move);

    // The watcher normally picks this up, but a folder that could not be
    // watched still has to refresh.
    RefreshTabsShowing(destDir);
    if (move) {
        for (const std::string& p : sources) RefreshTabsShowing(path::Parent(p));
    }
    SetStatus(strings_.Get("ui.done"));
    host_.Invalidate();
    return true;
}

void App::RefreshTabsShowing(const std::string& dir) {
    if (dir.empty()) return;
    Session* s = workspace_.activeSession();
    if (!s) return;
    for (Pane* p : s->Panes()) {
        if (Tab* t = p->activeTab()) {
            if (utf8::EqualsIgnoreCaseAscii(t->path, dir)) RequestLoad(*t, true);
        }
    }
}

void App::RebuildFocused() {
    if (Tab* t = workspace_.focusedTab()) {
        t->Rebuild();
        EnsureCursorVisible();
        host_.Invalidate();
    }
}

void App::EnsureCursorVisible() {
    Pane* p = workspace_.focusedPane();
    if (!p) return;
    Tab* t = p->activeTab();
    if (!t) return;

    const float rowH = p->rowHeight > 0.0f ? p->rowHeight : theme_.rowHeight;
    const float viewH = p->listHeight > 0.0f ? p->listHeight : rowH * 10.0f;
    const float top = static_cast<float>(t->cursor) * rowH;

    if (top < t->scroll) {
        t->scroll = top;
    } else if (top + rowH > t->scroll + viewH) {
        t->scroll = top + rowH - viewH;
    }
    const float maxScroll =
        std::max(0.0f, static_cast<float>(t->visible.size()) * rowH - viewH);
    t->scroll = std::clamp(t->scroll, 0.0f, maxScroll);
}

void App::MoveCursor(int delta, bool extend, bool absolute) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->visible.empty()) return;

    int target = absolute ? delta : t->cursor + delta;
    target = std::clamp(target, 0, static_cast<int>(t->visible.size()) - 1);

    if (extend) {
        t->ExtendTo(target);
    } else {
        // Marks deliberately survive a plain move. Space marks one row and steps
        // on; if walking to the next row wiped that mark, only neighbours could
        // ever be selected together. SelectNone (and any click on a row) clears.
        t->cursor = target;
        t->ResetAnchor();
    }
    EnsureCursorVisible();
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void App::SetStatus(const std::string& message) {
    statusMessage_ = message;
    statusUntilMs_ = plat::NowMs() + kStatusDurationMs;
    // A message nobody asked to be drawn is not a message. Most callers move
    // something else as well and would repaint anyway, but the ones that answer
    // without touching the listing - Ctrl+C, Ctrl+X, an empty undo - leave the
    // screen exactly as it was, so their answer used to sit there invisible
    // until an unrelated keystroke happened to redraw. The expiry timer has the
    // same dependency: the window only arms it while painting.
    host_.Invalidate();
}

bool App::statusExpired() const { return plat::NowMs() > statusUntilMs_; }

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------

bool App::HasBookmark(const std::string& p) const {
    for (const Bookmark& b : workspace_.bookmarks) {
        if (utf8::EqualsIgnoreCaseAscii(b.path, p)) return true;
    }
    return false;
}

void App::ToggleBookmark(const std::string& p) {
    for (size_t i = 0; i < workspace_.bookmarks.size(); ++i) {
        if (utf8::EqualsIgnoreCaseAscii(workspace_.bookmarks[i].path, p)) {
            workspace_.bookmarks.erase(workspace_.bookmarks.begin() + i);
            dirty_ = true;
            host_.Invalidate();
            return;
        }
    }
    workspace_.bookmarks.push_back({ path::DisplayName(p), p });
    dirty_ = true;
    host_.Invalidate();
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

void App::ShowContextMenuAt(int screenX, int screenY, bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    // A cursor parked on ".." has no selection to offer, so this falls through to
    // the folder being listed - and to the menu an empty-space right-click gives,
    // which is a different menu from the one the folder gets as an item.
    std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) {
        ShowShellMenu({ t->path }, screenX, screenY, extended, true);
        return;
    }
    ShowShellMenu(paths, screenX, screenY, extended, false);
}

void App::ShowFolderContextMenu(bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->path.empty()) return;
    // Deliberately ignores the selection: this command exists precisely because
    // a selected file otherwise hides the folder the user is looking at.
    int x = -1;
    int y = -1;
    CursorRowAnchor(x, y);
    // The item menu, which is the folder's own full menu - Open, Send to, Copy,
    // Delete, 7-Zip, Restore previous versions. The background menu was tried
    // here and is the wrong one: it answers for the space inside the folder, so
    // it carries New and Paste and nothing that acts on the folder itself, and
    // the result reads as a menu with most of its entries missing. What the
    // background menu was meant to keep out - "Git clone into this folder" -
    // turned out to follow CMF_EXTENDEDVERBS, not this choice, and is already
    // where Explorer keeps it: behind Shift.
    ShowShellMenu({ t->path }, x, y, extended, false);
}

void App::ShowBackgroundContextMenu(int screenX, int screenY, bool extended) {
    Tab* t = workspace_.focusedTab();
    if (!t || t->path.empty()) return;
    // The cursor row is not what was clicked, so it must not answer: pointing at
    // the empty space means the folder itself, as the space inside it.
    ShowShellMenu({ t->path }, screenX, screenY, extended, true);
}

void App::ShowShellMenu(const std::vector<std::string>& paths, int screenX, int screenY,
                        bool extended, bool background) {
    if (paths.empty()) return;
    if (!shell_.ShowContextMenu(paths, screenX, screenY, extended, background, theme_.dark)) {
        // The menu runs in a separate process; losing it means that process
        // could not be started, or a shell extension took it down. Say so rather
        // than letting a right-click look like it did nothing.
        SetStatus(strings_.Get("ui.shell_menu_failed"));
        return;
    }
    // The menu may have renamed, deleted or created something.
    RefreshFocused();
}

bool App::CursorRowAnchor(int& screenX, int& screenY) {
    Pane* pane = workspace_.focusedPane();
    if (!pane || pane->listArea.empty()) return false;
    const Tab* t = pane->activeTab();
    if (!t) return false;

    const RectF& area = pane->listArea;
    const float rowH = pane->rowHeight > 0.0f ? pane->rowHeight : theme_.rowHeight;

    // Bottom-left of the cursor row, which is where Windows itself drops the
    // menu for a focused list item. An off-screen or absent cursor falls back to
    // the top of the list rather than to a point outside the pane.
    float y = area.t;
    if (t->cursor >= 0 && t->cursor < static_cast<int>(t->visible.size())) {
        y = area.t + static_cast<float>(t->cursor + 1) * rowH - t->scroll;
    }
    y = std::clamp(y, area.t, area.b);

    return host_.ClientToScreen(area.l + kMenuAnchorIndent, y, screenX, screenY);
}

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------

void App::BeginPrompt(PromptKind kind, const char* labelKey, const std::string& initial) {
    prompt_.kind = kind;
    prompt_.labelKey = labelKey;
    prompt_.text = initial;
    prompt_.SetCaret(initial.size());
    prompt_.pendingPaths.clear();
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();
    // Primed but folded: the address bar opens on the folder that is already on
    // screen, and listing its children before a single key is pressed would put
    // a menu over the list for a question nobody asked.
    if (kind == PromptKind::Path) complete_.SetInput(initial);
    host_.Invalidate();
}

void App::CancelPrompt() {
    if (prompt_.kind == PromptKind::Filter) {
        if (Tab* t = workspace_.focusedTab()) {
            t->filter.clear();
            t->Rebuild();
            EnsureCursorVisible();
        }
    }
    prompt_ = Prompt{};
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();
    host_.Invalidate();
}

// Feed the completion whatever is in the field now.
//
// Completion always finishes the tail of the text, so it only makes sense while
// the caret is sitting at the end of it. With the caret parked in the middle,
// candidates for the tail would be answering a question the user is not asking,
// and adopting one would overwrite what comes after the caret.
void App::SyncCompletion(bool open) {
    if (prompt_.kind != PromptKind::Path) return;
    if (prompt_.caret != prompt_.text.size()) {
        complete_.Close();
        return;
    }
    complete_.SetInput(prompt_.text);
    if (open) complete_.Open();
    RequestCompletion();
}

void App::RequestCompletion() {
    if (!loader_ || !complete_.wantsListing()) return;
    // One request per directory. Typing "C:\\Users\\ab" walks through three
    // prefixes of one folder, and re-listing it for each keystroke would put a
    // network share's latency on every letter.
    if (completeToken_ != 0 && completeRequested_ == complete_.dir()) return;
    completeRequested_ = complete_.dir();
    completeToken_ = loader_->Request(complete_.dir());
}

bool App::MoveCompletion(int delta) {
    if (prompt_.kind != PromptKind::Path) return false;
    if (!complete_.open()) {
        // The first Tab is what opens the list, so nothing has been enumerated
        // yet. Ask now; the keystroke that follows finds the candidates there.
        SyncCompletion(true);
    }
    // Still folded means the text is not something completion answers for - the
    // caret is not at the end. The candidates from before are stale, so taking
    // one now would overwrite the tail the caret is sitting in front of.
    if (!complete_.open()) return false;
    if (!complete_.Move(delta)) return false;
    prompt_.text = complete_.text();
    prompt_.SetCaret(prompt_.text.size());
    host_.Invalidate();
    return true;
}

void App::CancelPathEdit() {
    if (prompt_.kind != PromptKind::Path) return;
    CancelPrompt();
}

void App::ChooseCompletion(int index) {
    if (prompt_.kind != PromptKind::Path) return;
    if (!complete_.Select(index)) return;
    prompt_.text = complete_.text();
    prompt_.SetCaret(prompt_.text.size());
    ApplyPrompt();
}

void App::ApplyPrompt() {
    Tab* t = workspace_.focusedTab();
    const PromptKind kind = prompt_.kind;
    const std::string text = prompt_.text;
    std::vector<std::string> pending = prompt_.pendingPaths;
    prompt_ = Prompt{};
    complete_.Reset();
    completeToken_ = 0;
    completeRequested_.clear();

    std::string err;
    switch (kind) {
        case PromptKind::Path:
            NavigateFocused(text);
            break;

        case PromptKind::Filter:
            if (t) ActivateEntry(t->cursor, false);
            break;

        case PromptKind::Rename: {
            if (!t || text.empty()) break;
            const fs::Entry* e = t->CursorEntry();
            if (!e || text == e->name) break;
            const std::string from = path::Join(t->path, e->name);
            const std::string to = path::Join(t->path, text);
            if (!fs_.Rename(from, to, &err)) {
                SetStatus(err);
            } else {
                undo_.Push({ UndoKind::Rename, { to }, { from } });
                RefreshFocused();
            }
            break;
        }

        case PromptKind::NewFolder: {
            if (!t || text.empty()) break;
            const std::string created = path::Join(t->path, text);
            if (!fs_.MakeDirectory(created, &err)) {
                SetStatus(err);
            } else {
                undo_.Push({ UndoKind::Create, { created }, {} });
                RefreshFocused();
            }
            break;
        }

        case PromptKind::NewFile: {
            if (!t || text.empty()) break;
            const std::string created = path::Join(t->path, text);
            if (!fs_.MakeFile(created, &err)) {
                SetStatus(err);
            } else {
                undo_.Push({ UndoKind::Create, { created }, {} });
                RefreshFocused();
            }
            break;
        }

        case PromptKind::SessionName: {
            if (Session* s = workspace_.activeSession()) {
                if (!text.empty()) s->name = text;
                dirty_ = true;
            }
            break;
        }

        case PromptKind::ConfirmDelete:
        case PromptKind::ConfirmDeletePermanent: {
            const bool recycle = (kind == PromptKind::ConfirmDelete);
            if (!fs_.Delete(pending, recycle, &err)) {
                SetStatus(err);
            } else {
                // 削除を戻す道はまだ無い（ごみ箱は仮想フォルダの側の話で、
                // ROADMAP P2-1 と一緒に入る）。だからこそ印を積む ─ 積まずに
                // 黙っていると、次の Ctrl+Z が削除を飛び越えて、消えたファイルは
                // そのままにその前の名前変更だけを巻き戻す。
                undo_.Push({ UndoKind::Delete, {}, {} });
                SetStatus(strings_.Get("ui.done"));
            }
            RefreshFocused();
            break;
        }

        case PromptKind::None:
            break;
    }
    host_.Invalidate();
}

bool App::HandlePromptKey(const Chord& chord) {
    if (!prompt_.active()) return false;

    // While filtering, the list must stay drivable from the keyboard.
    if (prompt_.kind == PromptKind::Filter) {
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::CursorUp || c == Cmd::CursorDown || c == Cmd::CursorPageUp ||
            c == Cmd::CursorPageDown || c == Cmd::CursorTop || c == Cmd::CursorBottom) {
            return false;
        }
    }

    auto syncFilter = [&] {
        if (prompt_.kind != PromptKind::Filter) return;
        if (Tab* t = workspace_.focusedTab()) {
            t->filter = prompt_.text;
            t->Rebuild();
            EnsureCursorVisible();
        }
    };

    // The field is a text field, so it answers the clipboard keys itself. Left
    // to the keymap they would reach Cmd::Copy and Cmd::Paste instead and act on
    // the listing behind the field - copying files while the caret sits in a
    // half-typed path is never what the keystroke meant.
    auto copyField = [&](bool cut) {
        // Nothing selected still leaves an obvious thing to copy - the whole
        // line - and copying cannot lose anything. Cut can, so it stays out of
        // the way until there is a range to take.
        std::string text;
        if (prompt_.hasSelection()) {
            text = prompt_.text.substr(prompt_.selBegin(), prompt_.selEnd() - prompt_.selBegin());
        } else if (!cut) {
            text = prompt_.text;
        }
        if (text.empty()) return;
        if (!shell_.SetClipboardText(text)) return;
        if (cut) {
            prompt_.DeleteSelection();
            syncFilter();
            SyncCompletion(true);
        }
        host_.Invalidate();
    };

    auto pasteField = [&] {
        std::string text;
        std::vector<std::string> files;
        // Files come second: a path copied as text is the common case, and the
        // shell hands back both formats when Explorer copied a file, so asking
        // for text first is what keeps "Copy as path" pasting as itself.
        if (!shell_.GetClipboardText(text)) {
            if (!shell_.GetClipboardFiles(files, nullptr) || files.empty()) return;
            text = files.front();
        }
        const std::string insert = ClipboardToField(text);
        if (insert.empty()) return;
        prompt_.DeleteSelection();
        prompt_.text.insert(prompt_.caret, insert);
        prompt_.SetCaret(prompt_.caret + insert.size());
        syncFilter();
        SyncCompletion(true);
        host_.Invalidate();
    };

    switch (chord.key) {
        case Key::Escape:
            // The candidate list goes first. Its whole point is to be dismissed
            // without losing what has been typed so far.
            if (complete_.open()) {
                complete_.Close();
                host_.Invalidate();
                return true;
            }
            CancelPrompt();
            return true;
        case Key::Enter:
            ApplyPrompt();
            return true;
        case Key::Tab:
        case Key::Down:
        case Key::Up: {
            if (prompt_.kind != PromptKind::Path) break;
            const bool back =
                (chord.key == Key::Up) || (chord.key == Key::Tab && (chord.mods & kModShift) != 0);
            MoveCompletion(back ? -1 : 1);
            return true;
        }
        case Key::Backspace: {
            if (prompt_.isConfirm()) return true;
            if (prompt_.DeleteSelection()) {
                syncFilter();
                SyncCompletion(true);
            } else if (prompt_.caret > 0) {
                const size_t start = utf8::PrevBoundary(prompt_.text, prompt_.caret);
                prompt_.text.erase(start, prompt_.caret - start);
                prompt_.SetCaret(start);
                syncFilter();
                SyncCompletion(true);
            }
            host_.Invalidate();
            return true;
        }
        case Key::Delete: {
            if (prompt_.isConfirm()) return true;
            // Shift+Delete is the other spelling of cut, the way Shift+Insert is
            // the other spelling of paste.
            if ((chord.mods & kModShift) != 0) {
                copyField(true);
                return true;
            }
            if (prompt_.DeleteSelection()) {
                syncFilter();
                SyncCompletion(true);
            } else if (prompt_.caret < prompt_.text.size()) {
                const size_t end = utf8::NextBoundary(prompt_.text, prompt_.caret);
                prompt_.text.erase(prompt_.caret, end - prompt_.caret);
                syncFilter();
                SyncCompletion(true);
            }
            host_.Invalidate();
            return true;
        }
        case Key::A:
            // Select all. The chord is Cmd::SelectAll everywhere else, and it
            // means the same thing here - the field is what has focus.
            if (chord.mods != kModCtrl || prompt_.isConfirm()) break;
            prompt_.SelectAll();
            host_.Invalidate();
            return true;
        case Key::C:
        case Key::X:
            if (chord.mods != kModCtrl || prompt_.isConfirm()) break;
            copyField(chord.key == Key::X);
            return true;
        case Key::V:
            if (chord.mods != kModCtrl || prompt_.isConfirm()) break;
            pasteField();
            return true;
        case Key::Insert:
            if (prompt_.isConfirm()) break;
            if (chord.mods == kModCtrl) {
                copyField(false);
            } else if (chord.mods == kModShift) {
                pasteField();
            }
            return true;
        case Key::Left:
        case Key::Right: {
            const bool back = (chord.key == Key::Left);
            const bool extend = (chord.mods & kModShift) != 0;
            size_t pos;
            if ((chord.mods & kModCtrl) != 0) {
                // Ctrl moves by path component. In a field that holds a path,
                // that is what a "word" is - stopping inside "Users" is never
                // what anyone reaches for Ctrl+arrow to do.
                pos = back ? path::PrevSegment(prompt_.text, prompt_.caret)
                           : path::NextSegment(prompt_.text, prompt_.caret);
            } else if (!extend && prompt_.hasSelection()) {
                // Without Shift, an arrow collapses the selection to its edge
                // rather than also eating a character.
                pos = back ? prompt_.selBegin() : prompt_.selEnd();
            } else {
                pos = back ? utf8::PrevBoundary(prompt_.text, prompt_.caret)
                           : utf8::NextBoundary(prompt_.text, prompt_.caret);
            }
            // Shift keeps the anchor where it was, which is what makes a run of
            // Shift+arrow grow one selection instead of a series of them.
            if (extend) {
                prompt_.caret = pos;
            } else {
                prompt_.SetCaret(pos);
            }
            SyncCompletion(false);
            host_.Invalidate();
            return true;
        }
        case Key::Home:
        case Key::End: {
            const size_t pos = (chord.key == Key::Home) ? 0 : prompt_.text.size();
            if ((chord.mods & kModShift) != 0) {
                prompt_.caret = pos;
            } else {
                prompt_.SetCaret(pos);
            }
            SyncCompletion(false);
            host_.Invalidate();
            return true;
        }
        default:
            break;
    }
    // Swallow everything else so a stray shortcut cannot fire mid-edit.
    return true;
}

bool App::OnChar(uint32_t cp) {
    // 設定画面は絞り込みを持たないが、打鍵は残らず飲み込む ─ 出しっぱなしの
    // 画面の裏でプロンプトが文字を受け取っては困る。
    if (settingsEditor_.visible()) return true;
    if (keyEditor_.visible()) {
        if (!keyEditor_.HandleChar(cp, strings_, keymap_)) return false;
        host_.Invalidate();
        return true;
    }
    if (!prompt_.active() || prompt_.isConfirm()) return false;
    if (cp < 0x20 || cp == 0x7F) return false;

    std::string encoded;
    utf8::Encode(cp, encoded);
    prompt_.DeleteSelection();
    prompt_.text.insert(prompt_.caret, encoded);
    prompt_.SetCaret(prompt_.caret + encoded.size());

    if (prompt_.kind == PromptKind::Filter) {
        if (Tab* t = workspace_.focusedTab()) {
            t->filter = prompt_.text;
            t->Rebuild();
            EnsureCursorVisible();
        }
    }
    SyncCompletion(true);
    host_.Invalidate();
    return true;
}

bool App::OnKey(const Chord& chord) {
    if (settingsEditor_.visible()) {
        // 開いたキーがそのまま閉じるキーになる。ショートカットの設定へ抜ける道も
        // 開けておく ─ 設定画面から「キーの設定」へ行けないのは不親切なだけ。
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::ShowSettings || c == Cmd::ShowKeySettings || c == Cmd::ShowKeyHelp) {
            Execute(c);
            return true;
        }
        const bool consumed = settingsEditor_.HandleKey(chord, strings_);
        ApplyPendingSetting();
        host_.Invalidate();
        return consumed;
    }
    if (keyEditor_.visible()) {
        // Its own chord still toggles it shut, so the key that opened the screen
        // is also the key that leaves it. Everything else the editor decides -
        // including whether it is currently swallowing the whole keyboard to
        // capture a new binding.
        if (!keyEditor_.capturing() && keymap_.Lookup(chord) == Cmd::ShowKeySettings) {
            Execute(Cmd::ShowKeySettings);
            return true;
        }
        const bool consumed = keyEditor_.HandleKey(chord, keymap_, strings_);
        SaveKeysIfChanged();
        // Escape closes the screen from the inside; take the same exit as the
        // command would have, so the confirmation is not lost.
        if (!keyEditor_.visible()) CloseKeyEditor();
        host_.Invalidate();
        return consumed;
    }
    if (keyHelp_) {
        // Any key closes the cheat sheet except the one that re-opens it.
        const Cmd c = keymap_.Lookup(chord);
        if (c == Cmd::ShowKeyHelp) return true;
        keyHelp_ = false;
        host_.Invalidate();
        if (chord.key == Key::Escape) return true;
    }
    if (HandlePromptKey(chord)) return true;

    const Cmd cmd = keymap_.Lookup(chord);
    if (cmd == Cmd::None) return false;
    Execute(cmd);
    return true;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void App::GotoTab(int index) {
    Pane* p = workspace_.focusedPane();
    if (!p) return;
    if (index < 0) index = static_cast<int>(p->tabs.size()) - 1;
    if (index >= static_cast<int>(p->tabs.size())) return;
    p->Activate(index);
    if (Tab* t = p->activeTab()) RequestLoad(*t);
    host_.Invalidate();
}

void App::GotoSession(int index) {
    if (index < 0 || index >= static_cast<int>(workspace_.sessions.size())) return;
    workspace_.ActivateSession(index);
    EnsureVisibleTabsLoaded();
    dirty_ = true;
    host_.Invalidate();
}

void App::GotoBookmark(int index) {
    if (index < 0 || index >= static_cast<int>(workspace_.bookmarks.size())) return;
    NavigateFocused(workspace_.bookmarks[index].path);
}

void App::DoDelete(bool permanent) {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    std::vector<std::string> paths = t->SelectionPaths();
    if (paths.empty()) {
        SetStatus(strings_.Get("ui.no_selection"));
        return;
    }
    BeginPrompt(permanent ? PromptKind::ConfirmDeletePermanent : PromptKind::ConfirmDelete,
                permanent ? "ui.confirm_delete_perm" : "ui.confirm_delete", {});
    prompt_.pendingPaths = paths;
    prompt_.text = std::to_string(paths.size());
}

void App::DoPaste() {
    Tab* t = workspace_.focusedTab();
    if (!t) return;
    std::vector<std::string> paths;
    bool cut = false;
    if (!shell_.GetClipboardFiles(paths, &cut) || paths.empty()) {
        SetStatus(strings_.Get("ui.clipboard_empty"));
        return;
    }
    const std::string dest = t->path;
    const std::vector<bool> existedBefore = DestinationsExist(fs_, paths, dest);

    std::string err;
    if (!fs_.CopyTo(paths, dest, cut, &err)) {
        SetStatus(err);
    } else {
        RecordTransfer(paths, dest, existedBefore, cut);
    }
    RefreshFocused();
}

void App::RecordTransfer(const std::vector<std::string>& sources, const std::string& destDir,
                         const std::vector<bool>& existedBefore, bool move) {
    UndoAction action;
    action.kind = move ? UndoKind::Move : UndoKind::Copy;
    for (size_t i = 0; i < sources.size(); ++i) {
        // Already there before, so whatever sits at that name now is either the
        // file that was always there or one the shell overwrote - not something
        // this operation is entitled to move away or throw out.
        if (i < existedBefore.size() && existedBefore[i]) continue;
        const std::string dest = path::Join(destDir, path::FileName(sources[i]));
        if (!fs_.Exists(dest)) continue;
        action.targets.push_back(dest);
        action.origins.push_back(sources[i]);
    }
    if (action.targets.empty()) return;
    if (!move) action.origins.clear();  // a copy has nowhere to go back to
    undo_.Push(std::move(action));
}

void App::DoUndo() {
    const UndoAction* next = undo_.top();
    if (!next) {
        SetStatus(strings_.Get("ui.undo_empty"));
        return;
    }
    if (next->kind == UndoKind::Delete) {
        // The mark stays put rather than being popped: everything under it was
        // dropped when it went on, so stepping past it would reach nothing, and
        // saying so every time is the only honest answer left.
        SetStatus(strings_.Get("ui.undo_no_delete"));
        return;
    }

    const UndoAction action = *next;
    undo_.Pop();

    std::vector<std::string> refresh;
    auto note = [&refresh](const std::string& dir) {
        if (dir.empty()) return;
        for (const std::string& d : refresh) {
            if (utf8::EqualsIgnoreCaseAscii(d, dir)) return;
        }
        refresh.push_back(dir);
    };

    std::string err;
    bool acted = false;
    const char* messageKey = "ui.undone_move";

    switch (action.kind) {
        case UndoKind::Rename: {
            messageKey = "ui.undone_rename";
            if (action.targets.empty() || action.origins.empty()) break;
            // Renamed again from somewhere else, or the old name taken back in
            // the meantime: this entry no longer describes the disk.
            if (!fs_.Exists(action.targets[0]) || fs_.Exists(action.origins[0])) break;
            if (!fs_.Rename(action.targets[0], action.origins[0], &err)) break;
            note(path::Parent(action.origins[0]));
            note(path::Parent(action.targets[0]));
            acted = true;
            break;
        }

        case UndoKind::Create:
        case UndoKind::Copy: {
            messageKey = action.kind == UndoKind::Create ? "ui.undone_create" : "ui.undone_copy";
            std::vector<std::string> alive;
            for (const std::string& p : action.targets) {
                if (fs_.Exists(p)) alive.push_back(p);
            }
            if (alive.empty()) break;
            // To the Recycle Bin, never permanently. A folder created an hour
            // ago can have been filled since, and undo is not a delete key.
            if (!fs_.Delete(alive, true, &err)) break;
            for (const std::string& p : alive) note(path::Parent(p));
            acted = true;
            break;
        }

        case UndoKind::Move: {
            // Grouped by the folder each item came from: one call per folder,
            // not per file, or the shell's progress dialog opens and closes once
            // for every item that was moved.
            std::vector<std::pair<std::string, std::vector<std::string>>> groups;
            for (size_t i = 0; i < action.targets.size() && i < action.origins.size(); ++i) {
                if (!fs_.Exists(action.targets[i])) continue;
                const std::string parent = path::Parent(action.origins[i]);
                if (parent.empty()) continue;
                auto it = groups.begin();
                for (; it != groups.end(); ++it) {
                    if (utf8::EqualsIgnoreCaseAscii(it->first, parent)) break;
                }
                if (it == groups.end()) {
                    groups.push_back({ parent, {} });
                    it = groups.end() - 1;
                }
                it->second.push_back(action.targets[i]);
                note(path::Parent(action.targets[i]));
                note(parent);
            }
            // All of them or none of the message: a half-moved undo that reports
            // success leaves the user believing the folders are back as they
            // were, which is the one thing they must not believe.
            acted = !groups.empty();
            for (const auto& group : groups) {
                if (fs_.CopyTo(group.second, group.first, true, &err)) continue;
                acted = false;
                break;
            }
            break;
        }

        case UndoKind::Delete:
            break;  // handled above
    }

    for (const std::string& dir : refresh) RefreshTabsShowing(dir);
    // The entry is spent either way - an undo that cannot be applied is not one
    // that gets more applicable by being tried again.
    SetStatus(acted ? strings_.Get(messageKey)
                    : (err.empty() ? strings_.Get("ui.undo_stale") : err));
    host_.Invalidate();
}

void App::Execute(Cmd cmd) {
    Session* session = workspace_.activeSession();
    Pane* pane = workspace_.focusedPane();
    Tab* tab = workspace_.focusedTab();

    switch (cmd) {
        // --- application -----------------------------------------------------
        case Cmd::NewWindow:
            // 表示中のフォルダを引き継ぐ。開けなかったことは黙って捨てない
            // ─ 何も起きないのと、増えないのとは別の話。
            if (!host_.OpenNewWindow(tab ? tab->path : std::string())) {
                SetStatus(strings_.Get("ui.new_window_failed"));
            }
            break;
        case Cmd::Quit:
            host_.Close();
            break;
        case Cmd::ReloadConfig: {
            LoadConfig();
            SetStatus(strings_.Get("ui.config_reloaded"));
            host_.Invalidate();
            break;
        }
        case Cmd::OpenConfigFolder:
            shell_.Open(fs_.ConfigDir());
            break;
        case Cmd::ToggleTheme:
            darkTheme_ = !darkTheme_;
            ApplyTheme();
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::ToggleLanguage: {
            language_ = (strings_.code() == "ja") ? "en" : "ja";
            LoadLanguage();
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ShowKeyHelp:
            keyHelp_ = !keyHelp_;
            if (keyHelp_) {
                keyEditor_.Close();
                settingsEditor_.Close();
            }
            host_.Invalidate();
            break;
        case Cmd::ShowKeySettings:
            if (keyEditor_.visible()) {
                CloseKeyEditor();
            } else {
                // The read-only sheet and the editor show the same table; two of
                // them on screen at once would only be confusing.
                keyHelp_ = false;
                settingsEditor_.Close();
                keyEditor_.Open(strings_, keymap_);
            }
            host_.Invalidate();
            break;
        case Cmd::ShowSettings:
            if (settingsEditor_.visible()) {
                settingsEditor_.Close();
            } else {
                keyHelp_ = false;
                keyEditor_.Close();
                // 開くたびに現在値から作り直す。設定は画面の外（Ctrl+Shift+M の
                // テーマ切り替え、Ctrl++ の文字サイズ）からも変わる。
                settingsEditor_.Open(strings_, CollectSettings());
                if (standalone_) SetStatus(strings_.Get("ui.settings_no_save"));
            }
            host_.Invalidate();
            break;
        case Cmd::CancelOverlay:
            if (settingsEditor_.visible()) {
                settingsEditor_.Close();
            } else if (keyEditor_.visible()) {
                CloseKeyEditor();
            } else if (keyHelp_) {
                keyHelp_ = false;
            } else if (prompt_.active()) {
                CancelPrompt();
            } else if (tab) {
                tab->ClearMarks();
                if (!tab->filter.empty()) {
                    tab->filter.clear();
                    tab->Rebuild();
                }
            }
            host_.Invalidate();
            break;

        // --- navigation ------------------------------------------------------
        case Cmd::GoUp:
            NavigateToParent(false);
            break;
        case Cmd::GoBack: {
            if (!tab || tab->back.empty()) break;
            const std::string target = tab->back.back();
            tab->back.pop_back();
            tab->forward.push_back(tab->path);
            tab->path = target;
            tab->loaded = false;
            tab->cursor = 0;
            tab->scroll = 0.0f;
            RequestLoad(*tab, true);
            host_.Invalidate();
            break;
        }
        case Cmd::GoForward: {
            if (!tab || tab->forward.empty()) break;
            const std::string target = tab->forward.back();
            tab->forward.pop_back();
            tab->back.push_back(tab->path);
            tab->path = target;
            tab->loaded = false;
            tab->cursor = 0;
            tab->scroll = 0.0f;
            RequestLoad(*tab, true);
            host_.Invalidate();
            break;
        }
        case Cmd::GoHome:
            NavigateFocused(fs_.HomeDir());
            break;
        case Cmd::GoRoot: {
            if (!tab) break;
            std::string p = tab->path;
            for (std::string up = path::Parent(p); !up.empty(); up = path::Parent(p)) p = up;
            NavigateFocused(p);
            break;
        }
        case Cmd::Refresh:
            RefreshRoots();
            RefreshFocused();
            break;
        case Cmd::OpenSelected:
            if (tab) ActivateEntry(tab->cursor, false);
            break;
        case Cmd::OpenInNewTab:
            if (tab) ActivateEntry(tab->cursor, true);
            break;
        case Cmd::EditPath:
            // No label: the bar this takes over already showed the path as
            // breadcrumbs, so a word in front of it only takes room from it.
            //
            // Selected whole, the way every address bar on the desktop opens:
            // the usual next move is to paste or type a different path over it,
            // and the wash is also what says the row stopped being breadcrumbs.
            if (tab) {
                BeginPrompt(PromptKind::Path, "", tab->path);
                prompt_.SelectAll();
            }
            break;
        case Cmd::FocusFilter:
            if (tab) BeginPrompt(PromptKind::Filter, "ui.filter_label", tab->filter);
            break;

        // --- cursor and selection --------------------------------------------
        case Cmd::CursorUp:        MoveCursor(-1, false); break;
        case Cmd::CursorDown:      MoveCursor(1, false); break;
        case Cmd::CursorPageUp:    MoveCursor(-(pane ? pane->rowsPerPage : 10), false); break;
        case Cmd::CursorPageDown:  MoveCursor(pane ? pane->rowsPerPage : 10, false); break;
        case Cmd::CursorTop:       MoveCursor(0, false, true); break;
        case Cmd::CursorBottom:
            MoveCursor(tab ? static_cast<int>(tab->visible.size()) - 1 : 0, false, true);
            break;
        case Cmd::ExtendUp:        MoveCursor(-1, true); break;
        case Cmd::ExtendDown:      MoveCursor(1, true); break;
        case Cmd::ExtendPageUp:    MoveCursor(-(pane ? pane->rowsPerPage : 10), true); break;
        case Cmd::ExtendPageDown:  MoveCursor(pane ? pane->rowsPerPage : 10, true); break;
        case Cmd::ExtendTop:       MoveCursor(0, true, true); break;
        case Cmd::ExtendBottom:
            MoveCursor(tab ? static_cast<int>(tab->visible.size()) - 1 : 0, true, true);
            break;
        case Cmd::ToggleSelection: {
            if (!tab || tab->visible.empty()) break;
            // Deliberately does not go through MoveCursor: toggling must
            // accumulate a multi-selection rather than reset it.
            const int index = tab->visible[tab->cursor];
            if (index >= 0) tab->marked[index] = tab->marked[index] ? 0 : 1;
            if (tab->cursor + 1 < static_cast<int>(tab->visible.size())) tab->cursor++;
            tab->ResetAnchor();
            EnsureCursorVisible();
            host_.Invalidate();
            break;
        }
        case Cmd::SelectAll:
            if (tab) {
                tab->MarkRange(0, static_cast<int>(tab->visible.size()) - 1, true);
                // Whatever range was being dragged out with Shift is over; the
                // next extension has to build on what is on screen now.
                tab->ResetAnchor();
                host_.Invalidate();
            }
            break;
        case Cmd::SelectNone:
            if (tab) {
                tab->ClearMarks();
                host_.Invalidate();
            }
            break;
        case Cmd::InvertSelection:
            if (tab) {
                for (int index : tab->visible) {
                    if (index < 0) continue;
                    tab->marked[index] = tab->marked[index] ? 0 : 1;
                }
                tab->ResetAnchor();
                host_.Invalidate();
            }
            break;

        // --- tabs ------------------------------------------------------------
        case Cmd::NewTab: {
            if (!pane) break;
            Tab* t = pane->AddTab(tab ? tab->path : fs_.HomeDir(), NewTabAt(*pane));
            t->view = defaultView_;
            RequestLoad(*t, true);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::CloseTab: {
            if (!pane) break;
            std::string closed;
            if (pane->CloseTab(pane->active, &closed)) {
                workspace_.closedTabs.push_back(closed);
                if (Tab* t = pane->activeTab()) RequestLoad(*t);
                dirty_ = true;
            }
            host_.Invalidate();
            break;
        }
        case Cmd::DuplicateTab: {
            if (!pane || !tab) break;
            Tab* t = pane->AddTab(tab->path, pane->active + 1);
            t->view = tab->view;
            RequestLoad(*t, true);
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::ReopenTab: {
            if (!pane || workspace_.closedTabs.empty()) break;
            const std::string p = workspace_.closedTabs.back();
            workspace_.closedTabs.pop_back();
            Tab* t = pane->AddTab(p, NewTabAt(*pane));
            t->view = defaultView_;
            RequestLoad(*t, true);
            host_.Invalidate();
            break;
        }
        case Cmd::NextTab:
            if (pane && !pane->tabs.empty()) {
                GotoTab((pane->active + 1) % static_cast<int>(pane->tabs.size()));
            }
            break;
        case Cmd::PrevTab:
            if (pane && !pane->tabs.empty()) {
                const int n = static_cast<int>(pane->tabs.size());
                GotoTab((pane->active - 1 + n) % n);
            }
            break;
        case Cmd::MoveTabLeft:
            if (pane && pane->active > 0) {
                std::swap(pane->tabs[pane->active], pane->tabs[pane->active - 1]);
                pane->active--;
                dirty_ = true;
                host_.Invalidate();
            }
            break;
        case Cmd::MoveTabRight:
            if (pane && pane->active + 1 < static_cast<int>(pane->tabs.size())) {
                std::swap(pane->tabs[pane->active], pane->tabs[pane->active + 1]);
                pane->active++;
                dirty_ = true;
                host_.Invalidate();
            }
            break;
        case Cmd::Tab1: GotoTab(0); break;
        case Cmd::Tab2: GotoTab(1); break;
        case Cmd::Tab3: GotoTab(2); break;
        case Cmd::Tab4: GotoTab(3); break;
        case Cmd::Tab5: GotoTab(4); break;
        case Cmd::Tab6: GotoTab(5); break;
        case Cmd::Tab7: GotoTab(6); break;
        case Cmd::Tab8: GotoTab(7); break;
        case Cmd::TabLast: GotoTab(-1); break;

        // --- panes -----------------------------------------------------------
        case Cmd::SplitLeftRight:
        case Cmd::SplitTopBottom: {
            if (!session || !pane) break;
            const SplitNode::Kind kind = (cmd == Cmd::SplitLeftRight)
                                             ? SplitNode::Kind::LeftRight
                                             : SplitNode::Kind::TopBottom;
            if (Pane* created = session->Split(pane, kind)) {
                if (Tab* t = created->activeTab()) {
                    t->view = tab ? tab->view : defaultView_;
                    RequestLoad(*t, true);
                }
                session->focus = created;
                dirty_ = true;
            }
            host_.Invalidate();
            break;
        }
        case Cmd::ClosePane:
            if (session && pane) {
                if (!session->ClosePane(pane)) {
                    SetStatus(strings_.Get("ui.cannot_close_last"));
                } else {
                    dirty_ = true;
                }
                host_.Invalidate();
            }
            break;
        case Cmd::FocusNextPane:
        case Cmd::FocusPrevPane: {
            if (!session) break;
            std::vector<Pane*> panes = session->Panes();
            if (panes.size() < 2) break;
            int index = 0;
            for (size_t i = 0; i < panes.size(); ++i) {
                if (panes[i] == pane) index = static_cast<int>(i);
            }
            const int n = static_cast<int>(panes.size());
            const int step = (cmd == Cmd::FocusNextPane) ? 1 : -1;
            FocusPane(panes[(index + step + n) % n]);
            break;
        }
        case Cmd::FocusPaneLeft:
            if (session && pane) FocusPane(session->PaneInDirection(pane, -1, 0));
            break;
        case Cmd::FocusPaneRight:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 1, 0));
            break;
        case Cmd::FocusPaneUp:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 0, -1));
            break;
        case Cmd::FocusPaneDown:
            if (session && pane) FocusPane(session->PaneInDirection(pane, 0, 1));
            break;
        case Cmd::OpenInOtherPane: {
            if (!session || !pane || !tab) break;
            std::vector<Pane*> panes = session->Panes();
            if (panes.size() < 2) break;
            const fs::Entry* e = tab->CursorEntry();
            const std::string target = (e && e->isDir()) ? path::Join(tab->path, e->name)
                                                         : tab->path;
            Pane* other = nullptr;
            for (size_t i = 0; i < panes.size(); ++i) {
                if (panes[i] == pane) {
                    other = panes[(i + 1) % panes.size()];
                    break;
                }
            }
            if (!other) break;
            if (Tab* ot = other->activeTab()) {
                ot->path = target;
                ot->loaded = false;
                ot->cursor = 0;
                ot->scroll = 0.0f;
                RequestLoad(*ot, true);
            }
            host_.Invalidate();
            break;
        }
        case Cmd::SyncOtherPane: {
            if (!session || !pane || !tab) break;
            for (Pane* p : session->Panes()) {
                if (p == pane) continue;
                if (Tab* ot = p->activeTab()) {
                    ot->path = tab->path;
                    ot->loaded = false;
                    ot->cursor = 0;
                    ot->scroll = 0.0f;
                    RequestLoad(*ot, true);
                }
            }
            host_.Invalidate();
            break;
        }
        case Cmd::SwapPanes:
            if (session && pane) {
                session->SwapWithSibling(pane);
                dirty_ = true;
                host_.Invalidate();
            }
            break;

        // --- sessions ---------------------------------------------------------
        case Cmd::NewSession: {
            const std::string name = strings_.Format(
                "ui.new_session", { std::to_string(workspace_.sessions.size() + 1) });
            Session* s = workspace_.AddSession(name, tab ? tab->path : fs_.HomeDir());
            for (Pane* p : s->Panes()) {
                for (std::unique_ptr<Tab>& t : p->tabs) t->view = defaultView_;
            }
            EnsureVisibleTabsLoaded();
            dirty_ = true;
            host_.Invalidate();
            break;
        }
        case Cmd::CloseSession:
            workspace_.CloseSession(workspace_.active);
            EnsureVisibleTabsLoaded();
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::RenameSession:
            if (session) BeginPrompt(PromptKind::SessionName, "ui.session_name_label", session->name);
            break;
        case Cmd::NextSession:
            if (!workspace_.sessions.empty()) {
                GotoSession((workspace_.active + 1) %
                            static_cast<int>(workspace_.sessions.size()));
            }
            break;
        case Cmd::PrevSession:
            if (!workspace_.sessions.empty()) {
                const int n = static_cast<int>(workspace_.sessions.size());
                GotoSession((workspace_.active - 1 + n) % n);
            }
            break;
        case Cmd::SaveWorkspace:
            // 失敗したときは WriteConfigFile が出した「保存できません」をそのまま
            // 残す ─ 書けていないのに「保存しました」と答えるのが最悪の応答。
            if (standalone_) {
                SetStatus(strings_.Get("ui.standalone_no_save"));
            } else if (SaveAll()) {
                SetStatus(strings_.Get("ui.saved"));
            }
            break;
        case Cmd::Session1: GotoSession(0); break;
        case Cmd::Session2: GotoSession(1); break;
        case Cmd::Session3: GotoSession(2); break;
        case Cmd::Session4: GotoSession(3); break;
        case Cmd::Session5: GotoSession(4); break;
        case Cmd::Session6: GotoSession(5); break;
        case Cmd::Session7: GotoSession(6); break;
        case Cmd::Session8: GotoSession(7); break;

        // --- view -------------------------------------------------------------
        case Cmd::ToggleHidden:
            if (tab) {
                tab->view.showHidden = !tab->view.showHidden;
                defaultView_.showHidden = tab->view.showHidden;
                RebuildFocused();
                dirty_ = true;
            }
            break;
        case Cmd::ToggleSidebar:
            sidebarVisible_ = !sidebarVisible_;
            dirty_ = true;
            host_.Invalidate();
            break;
        case Cmd::SortByName:
        case Cmd::SortByExt:
        case Cmd::SortBySize:
        case Cmd::SortByDate: {
            if (!tab) break;
            const SortKey key = (cmd == Cmd::SortByExt)    ? SortKey::Ext
                                : (cmd == Cmd::SortBySize) ? SortKey::Size
                                : (cmd == Cmd::SortByDate) ? SortKey::Date
                                                           : SortKey::Name;
            // Re-selecting the active column flips the direction, like Explorer.
            if (tab->view.sort == key) {
                tab->view.sortDesc = !tab->view.sortDesc;
            } else {
                tab->view.sort = key;
                tab->view.sortDesc = false;
            }
            defaultView_.sort = tab->view.sort;
            defaultView_.sortDesc = tab->view.sortDesc;
            RebuildFocused();
            dirty_ = true;
            break;
        }
        case Cmd::ToggleSortOrder:
            if (tab) {
                tab->view.sortDesc = !tab->view.sortDesc;
                defaultView_.sortDesc = tab->view.sortDesc;
                RebuildFocused();
                dirty_ = true;
            }
            break;
        case Cmd::ToggleDirsFirst:
            if (tab) {
                tab->view.dirsFirst = !tab->view.dirsFirst;
                defaultView_.dirsFirst = tab->view.dirsFirst;
                RebuildFocused();
                dirty_ = true;
            }
            break;
        case Cmd::FontLarger:
            SetFontScale(fontScale_ + kFontScaleStep);
            break;
        case Cmd::FontSmaller:
            SetFontScale(fontScale_ - kFontScaleStep);
            break;
        case Cmd::FontReset:
            SetFontScale(1.0f);
            break;

        // --- bookmarks ---------------------------------------------------------
        case Cmd::AddBookmark:
            if (tab) {
                if (!HasBookmark(tab->path)) ToggleBookmark(tab->path);
                dirty_ = true;
            }
            break;
        case Cmd::RemoveBookmark:
            if (tab && HasBookmark(tab->path)) {
                ToggleBookmark(tab->path);
                dirty_ = true;
            }
            break;
        case Cmd::Bookmark1: GotoBookmark(0); break;
        case Cmd::Bookmark2: GotoBookmark(1); break;
        case Cmd::Bookmark3: GotoBookmark(2); break;
        case Cmd::Bookmark4: GotoBookmark(3); break;
        case Cmd::Bookmark5: GotoBookmark(4); break;
        case Cmd::Bookmark6: GotoBookmark(5); break;
        case Cmd::Bookmark7: GotoBookmark(6); break;
        case Cmd::Bookmark8: GotoBookmark(7); break;

        // --- file operations ---------------------------------------------------
        case Cmd::NewFolder:
            BeginPrompt(PromptKind::NewFolder, "ui.new_folder_label", {});
            break;
        case Cmd::NewFile:
            BeginPrompt(PromptKind::NewFile, "ui.new_file_label", {});
            break;
        case Cmd::Rename: {
            if (!tab) break;
            const fs::Entry* e = tab->CursorEntry();
            if (!e) break;
            BeginPrompt(PromptKind::Rename, "ui.rename_label", e->name);
            // Preselect the stem so typing replaces the name, not the extension.
            if (!e->isDir()) prompt_.SetCaret(path::Stem(e->name).size());
            break;
        }
        case Cmd::DeleteToRecycle: DoDelete(false); break;
        case Cmd::DeletePermanent: DoDelete(true); break;
        case Cmd::Copy:
        case Cmd::Cut: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            if (paths.empty()) {
                SetStatus(strings_.Get("ui.no_selection"));
                break;
            }
            // Say which of the two happened, and say it at all: the clipboard
            // gives no sign of having been written, so a copy that silently
            // failed and one that worked look exactly alike from here.
            const bool cut = (cmd == Cmd::Cut);
            if (!shell_.SetClipboardFiles(paths, cut)) {
                SetStatus(strings_.Get("ui.clipboard_failed"));
                break;
            }
            SetStatus(strings_.Get(cut ? "ui.cut_files" : "ui.copied_files"));
            break;
        }
        case Cmd::Paste:
            DoPaste();
            break;
        case Cmd::Undo:
            DoUndo();
            break;
        case Cmd::CopyPath: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            std::string joined;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) joined += "\r\n";
                joined += paths[i];
            }
            if (joined.empty()) joined = tab->path;
            SetStatus(strings_.Get(shell_.SetClipboardText(joined) ? "ui.copied"
                                                                  : "ui.clipboard_failed"));
            break;
        }
        case Cmd::CopyName: {
            if (!tab) break;
            std::vector<std::string> paths = tab->SelectionPaths();
            std::string joined;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) joined += "\r\n";
                joined += path::FileName(paths[i]);
            }
            SetStatus(strings_.Get(shell_.SetClipboardText(joined) ? "ui.copied"
                                                                  : "ui.clipboard_failed"));
            break;
        }

        // --- shell -------------------------------------------------------------
        case Cmd::ContextMenu:
        case Cmd::ExtendedContextMenu: {
            // Position is supplied by the UI layer for mouse invocations; from
            // the keyboard it belongs on the row the user is looking at, not
            // wherever the pointer was left. A negative pair means "no anchor",
            // and the shell falls back to the pointer.
            int x = -1;
            int y = -1;
            CursorRowAnchor(x, y);
            ShowContextMenuAt(x, y, cmd == Cmd::ExtendedContextMenu);
            break;
        }
        case Cmd::FolderContextMenu:
        case Cmd::ExtendedFolderContextMenu:
            ShowFolderContextMenu(cmd == Cmd::ExtendedFolderContextMenu);
            break;
        case Cmd::Properties:
            if (tab) {
                const std::string p = tab->CursorPath();
                shell_.ShowProperties(p.empty() ? tab->path : p);
            }
            break;
        case Cmd::OpenWith:
            if (tab) {
                const std::string p = tab->CursorPath();
                if (!p.empty()) shell_.OpenWith(p);
            }
            break;
        case Cmd::RevealInExplorer:
            if (tab) {
                const std::string p = tab->CursorPath();
                shell_.RevealInExplorer(p.empty() ? tab->path : p);
            }
            break;
        case Cmd::OpenTerminal:
            if (tab) shell_.OpenTerminal(tab->path);
            break;
        case Cmd::ConnectNetwork: {
            if (!tab) break;
            // The connection is to the server or the share, never to a folder
            // inside it - that is the unit credentials are checked against.
            const std::string target = path::UncRoot(tab->path);
            if (target.empty()) {
                SetStatus(strings_.Get("ui.not_network_path"));
                break;
            }
            std::string err;
            if (!shell_.ConnectNetwork(target, &err)) {
                // An empty message means the user closed the dialog; they know
                // what happened, and saying it again reads as a failure report.
                if (!err.empty()) SetStatus(err);
                break;
            }
            SetStatus(strings_.Format("ui.network_connected", { target }));
            RefreshFocused();
            break;
        }

        case Cmd::None:
        case Cmd::Count:
            break;
    }

    // Cheap when nothing moved, and it keeps watches correct after any command
    // that adds, closes or retargets a pane or tab.
    SyncWatches();

    UpdateTitle();
}

}  // namespace kite
