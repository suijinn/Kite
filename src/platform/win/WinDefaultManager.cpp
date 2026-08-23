#include "platform/win/WinDefaultManager.h"

#include <windows.h>

#include <shlobj.h>

#include <string>
#include <vector>

#include "platform/win/WinPaths.h"

namespace kite::win {
namespace {

// The four keys, and what goes in each.
//
// The first two are the folder verb; the other two live under the File Explorer
// folder-shortcut CLSID, which is what Win+E launches. The CLSID is a separate
// mechanism from the folder verb: writing only the latter leaves Win+E going to
// Explorer, which is why the roadmap calls it out on its own (P2-11).
//
// "opennewwindow" as well as "open" because current Windows launches the shortcut
// through the former; older builds use the latter. Both are cheap to write and
// both are restored together, so there is no reason to guess which one this
// machine will ask for.
//
// **Folder is the class that actually opens folders, not Directory.** Writing
// Directory alone was tried and changed nothing: on Windows 11 there is no
// HKCR\Directory\shell\open at all, and HKCR\Directory\shell names "none" as its
// default verb - the verb a folder opens with is inherited from Folder, whose
// command is Explorer.exe behind a DelegateExecute handler. Directory is kept
// anyway because it costs one key and is the more specific class, so a shell that
// does consult it finds Kite first.
//
// Folder being the superclass means it also covers the shell's own places -
// Control Panel, This PC, a network location - and for those the "%1" is a parsing
// name rather than a path. That is what App::PathFromShell is for: "::{CLSID}"
// becomes a virtual path Kite already knows how to open, instead of a window
// reporting that the location is unavailable.
constexpr wchar_t kExplorerClsid[] = L"{52205fd8-5dfb-447d-801a-d0b52f2e83e1}";

constexpr wchar_t kBackupKey[] = L"Software\\Kite\\DefaultManagerBackup";
constexpr wchar_t kDelegateValue[] = L"DelegateExecute";

// The keys, in order. The CLSID pair is built rather than written out so the GUID
// appears exactly once.
const std::vector<std::wstring>& TargetKeys() {
    static const std::vector<std::wstring> keys = [] {
        const std::wstring clsid =
            std::wstring(L"Software\\Classes\\CLSID\\") + kExplorerClsid + L"\\shell\\";
        return std::vector<std::wstring>{
            L"Software\\Classes\\Directory\\shell\\open\\command",
            L"Software\\Classes\\Folder\\shell\\open\\command",
            clsid + L"open\\command",
            clsid + L"opennewwindow\\command",
        };
    }();
    return keys;
}

constexpr int kTargetCount = 4;

// The folder verbs are handed a folder; the Explorer shortcut is handed nothing,
// so a "%1" there would arrive as those two characters.
constexpr int kFolderVerbs = 2;
bool TakesPath(int index) { return index < kFolderVerbs; }

// --- small registry helpers ------------------------------------------------

bool ReadString(HKEY root, const std::wstring& key, const wchar_t* value, std::wstring& out) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(handle, value, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(handle);
        return false;
    }
    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(handle, value, nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    RegCloseKey(handle);
    if (status != ERROR_SUCCESS) return false;
    // The stored size includes the terminator on some writers and not others.
    buffer.resize(wcslen(buffer.c_str()));
    out = std::move(buffer);
    return true;
}

bool WriteString(HKEY root, const std::wstring& key, const wchar_t* value,
                 const std::wstring& data) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                        nullptr, &handle, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegSetValueExW(handle, value, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(data.c_str()), bytes);
    RegCloseKey(handle);
    return status == ERROR_SUCCESS;
}

bool WriteDword(HKEY root, const std::wstring& key, const wchar_t* value, DWORD data) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                        nullptr, &handle, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExW(handle, value, 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(handle);
    return status == ERROR_SUCCESS;
}

bool ReadDword(HKEY root, const std::wstring& key, const wchar_t* value, DWORD& out) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = sizeof(out);
    const LSTATUS status = RegQueryValueExW(handle, value, nullptr, &type,
                                            reinterpret_cast<BYTE*>(&out), &bytes);
    RegCloseKey(handle);
    return status == ERROR_SUCCESS && type == REG_DWORD;
}

bool KeyExists(HKEY root, const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

void DeleteValue(HKEY root, const std::wstring& key, const wchar_t* value) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_SET_VALUE, &handle) != ERROR_SUCCESS) return;
    RegDeleteValueW(handle, value);
    RegCloseKey(handle);
}

// Deletes a key only when nothing is left in it.
//
// RegDeleteKeyW refuses a key with subkeys but happily takes one that still holds
// values, so the value count has to be checked as well - otherwise pruning the
// empty scaffolding above a deleted key would take a key that meant something.
bool DeleteKeyIfEmpty(HKEY root, const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) return false;
    DWORD subkeys = 0;
    DWORD values = 0;
    const LSTATUS status =
        RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                         nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    if (status != ERROR_SUCCESS || subkeys != 0 || values != 0) return false;
    return RegDeleteKeyW(root, key.c_str()) == ERROR_SUCCESS;
}

// The key and whatever scaffolding above it Kite had to create to get there.
//
// Stops at Software\Classes: everything below it here was made by this file, and
// an empty key left behind would be harmless but is still litter. DeleteKeyIfEmpty
// stops on its own the moment a level still holds something - which is what keeps
// "open" alive while "opennewwindow" is still registered.
void DeleteKeyAndEmptyParents(const std::wstring& key) {
    static const std::wstring stop = L"Software\\Classes";
    HKEY root = HKEY_CURRENT_USER;
    RegDeleteKeyW(root, key.c_str());
    std::wstring path = key;
    for (;;) {
        const size_t cut = path.rfind(L'\\');
        if (cut == std::wstring::npos) return;
        path.resize(cut);
        if (path.size() <= stop.size()) return;
        if (!DeleteKeyIfEmpty(root, path)) return;
    }
}

// --- the command line ------------------------------------------------------

// What goes in the default value.
std::wstring CommandFor(const std::wstring& exe, bool takesPath) {
    std::wstring command = QuoteArgument(exe);
    if (takesPath) command += L" \"%1\"";
    return command;
}

// The executable a registered command line points at.
//
// Only the first token is wanted, and a quoted first token is the only form Kite
// ever writes - but a command put there by hand may not be quoted, so the bare
// form is read too.
std::wstring ExeFromCommand(const std::wstring& command) {
    if (command.empty()) return {};
    if (command.front() == L'"') {
        const size_t end = command.find(L'"', 1);
        if (end == std::wstring::npos) return command.substr(1);
        return command.substr(1, end - 1);
    }
    const size_t space = command.find(L' ');
    return space == std::wstring::npos ? command : command.substr(0, space);
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

bool SameTextIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                                        b.c_str(), static_cast<int>(b.size()),
                                                        TRUE) == CSTR_EQUAL;
}

// --- the backup ------------------------------------------------------------

// One subkey per registered key, named by its index but carrying the key path as
// a value. Restore reads the path from the backup rather than from the table
// above, so a build whose table has changed still undoes exactly what the build
// that registered actually wrote.
std::wstring BackupSlot(int index) {
    return std::wstring(kBackupKey) + L"\\" + std::to_wstring(index);
}

void CaptureBackup(int index, const std::wstring& key) {
    const std::wstring slot = BackupSlot(index);
    WriteString(HKEY_CURRENT_USER, slot, L"Key", key);

    std::wstring previous;
    const bool existed = KeyExists(HKEY_CURRENT_USER, key);
    WriteDword(HKEY_CURRENT_USER, slot, L"Existed", existed ? 1u : 0u);
    if (existed && ReadString(HKEY_CURRENT_USER, key, nullptr, previous)) {
        WriteString(HKEY_CURRENT_USER, slot, L"Command", previous);
    }
    std::wstring delegate;
    const bool hadDelegate =
        existed && ReadString(HKEY_CURRENT_USER, key, kDelegateValue, delegate);
    WriteDword(HKEY_CURRENT_USER, slot, L"HadDelegate", hadDelegate ? 1u : 0u);
    if (hadDelegate) WriteString(HKEY_CURRENT_USER, slot, L"Delegate", delegate);
}

// Puts one key back the way the backup says it was, then forgets the backup.
// @return true if there was a backup to act on
bool RestoreSlot(int index) {
    const std::wstring slot = BackupSlot(index);
    std::wstring key;
    if (!ReadString(HKEY_CURRENT_USER, slot, L"Key", key) || key.empty()) return false;

    DWORD existed = 0;
    ReadDword(HKEY_CURRENT_USER, slot, L"Existed", existed);
    if (existed == 0) {
        DeleteKeyAndEmptyParents(key);
    } else {
        std::wstring command;
        ReadString(HKEY_CURRENT_USER, slot, L"Command", command);
        WriteString(HKEY_CURRENT_USER, key, nullptr, command);
        DWORD hadDelegate = 0;
        ReadDword(HKEY_CURRENT_USER, slot, L"HadDelegate", hadDelegate);
        std::wstring delegate;
        if (hadDelegate != 0 && ReadString(HKEY_CURRENT_USER, slot, L"Delegate", delegate)) {
            WriteString(HKEY_CURRENT_USER, key, kDelegateValue, delegate);
        } else {
            DeleteValue(HKEY_CURRENT_USER, key, kDelegateValue);
        }
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, slot.c_str());
    return true;
}

// Undoes everything the backup knows about. Used both by the unregister command
// and by a half-finished register, which is why it walks the backup rather than
// the table: the slots that exist are exactly the keys that were touched.
void RestoreAll() {
    int restored = 0;
    for (int i = 0; i < kTargetCount; ++i) {
        if (RestoreSlot(i)) ++restored;
    }
    if (restored == 0) {
        // Registered, but with no record of what was there first - an older build,
        // or keys put in by hand. The keys still have to go, or "unregister" would
        // report success and change nothing. Only the ones naming a Kite: removing
        // a registration that belongs to another application would be doing
        // something nobody asked for.
        const std::wstring self = FileNameOf(ModuleFilePath());
        for (const std::wstring& key : TargetKeys()) {
            std::wstring command;
            if (!ReadString(HKEY_CURRENT_USER, key, nullptr, command)) continue;
            if (!SameTextIgnoreCase(FileNameOf(ExeFromCommand(command)), self)) continue;
            DeleteKeyAndEmptyParents(key);
        }
    }
    // Both levels, and only while empty - someone else's Software\Kite values are
    // not this function's to remove.
    DeleteKeyIfEmpty(HKEY_CURRENT_USER, kBackupKey);
    DeleteKeyIfEmpty(HKEY_CURRENT_USER, L"Software\\Kite");
}

void AnnounceChange() {
    // Without this the shell keeps handing folders to whatever it resolved before,
    // in some cases until the next logon.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace

DefaultManager DefaultManagerState() {
    const std::wstring exe = ModuleFilePath();
    if (exe.empty()) return DefaultManager::No;

    std::wstring command;
    if (!ReadString(HKEY_CURRENT_USER, TargetKeys()[0], nullptr, command)) {
        return DefaultManager::No;
    }
    const std::wstring registered = ExeFromCommand(command);
    if (registered.empty()) return DefaultManager::No;
    if (SameTextIgnoreCase(registered, exe)) return DefaultManager::Yes;
    // Same file name from somewhere else: a Kite that was moved, or a second copy.
    // Anything else is another application holding the registration, and that is
    // the user's choice rather than something to report.
    if (SameTextIgnoreCase(FileNameOf(registered), FileNameOf(exe))) {
        return DefaultManager::Other;
    }
    return DefaultManager::No;
}

bool SetDefaultManager(bool on) {
    if (!on) {
        RestoreAll();
        AnnounceChange();
        return true;
    }

    const std::wstring exe = ModuleFilePath();
    if (exe.empty()) return false;

    // Only when there is none. A backup taken while Kite already holds the keys
    // would record Kite's own command line as "what was there before", and the
    // unregister that followed would put Kite back.
    const bool freshBackup = !KeyExists(HKEY_CURRENT_USER, kBackupKey);

    const std::vector<std::wstring>& keys = TargetKeys();
    for (int i = 0; i < kTargetCount; ++i) {
        if (freshBackup) CaptureBackup(i, keys[static_cast<size_t>(i)]);
        const std::wstring command = CommandFor(exe, TakesPath(i));
        const bool written =
            WriteString(HKEY_CURRENT_USER, keys[static_cast<size_t>(i)], nullptr, command) &&
            // Blanked rather than left alone: HKCR merges HKCU over HKLM value by
            // value, so the DelegateExecute that ships with Windows stays visible
            // through a key that only overrides the default value - and it is that
            // handler, not the command line, that would then run.
            WriteString(HKEY_CURRENT_USER, keys[static_cast<size_t>(i)], kDelegateValue, L"");
        if (!written) {
            // Half-written is neither state. Back out through the same path the
            // unregister command uses, so there is only one way to undo this.
            RestoreAll();
            AnnounceChange();
            return false;
        }
    }
    AnnounceChange();
    return true;
}

}  // namespace kite::win
