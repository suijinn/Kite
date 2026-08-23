#include "platform/win/ShellFolder.h"

#include <shellapi.h>  // FOF_NO_UI. <shlobj.h> alone leaves it undefined.
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <vector>

#include "platform/win/WinUtf.h"

namespace kite::win {
namespace {

// SEH guards around the calls that instantiate other people's code.
//
// Raw pointers only, so nothing here needs C++ unwinding and __try is legal.
// Same arrangement as ShellMenu.cpp and ShellIcons.cpp.
HRESULT GuardedEnumObjects(IShellFolder* folder, SHCONTF flags, IEnumIDList** out) {
    __try {
        return folder->EnumObjects(nullptr, flags, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT GuardedNext(IEnumIDList* enumerator, PITEMID_CHILD* child) {
    __try {
        ULONG fetched = 0;
        const HRESULT hr = enumerator->Next(1, child, &fetched);
        if (FAILED(hr)) return hr;
        return (hr == S_OK && fetched == 1) ? S_OK : S_FALSE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT GuardedDisplayName(IShellFolder* folder, PCUITEMID_CHILD child, SHGDNF flags,
                           STRRET* out) {
    __try {
        return folder->GetDisplayNameOf(child, flags, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

// The copy itself reads through the namespace extension, so it is third-party
// code with a Microsoft wrapper around it and gets the same guard as the rest.
HRESULT GuardedPerform(IFileOperation* op) {
    __try {
        return op->PerformOperations();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

HRESULT GuardedAttributes(IShellFolder* folder, PCUITEMID_CHILD child, SFGAOF* flags) {
    __try {
        return folder->GetAttributesOf(1, &child, flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

// One COM pointer, released on scope exit.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() {
        if (ptr_) ptr_->Release();
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put() { return &ptr_; }
    void** putVoid() { return reinterpret_cast<void**>(&ptr_); }
    T* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

// One absolute PIDL from the task allocator, freed on scope exit.
class Pidl {
public:
    Pidl() = default;
    ~Pidl() {
        if (ptr_) ::CoTaskMemFree(ptr_);
    }
    Pidl(const Pidl&) = delete;
    Pidl& operator=(const Pidl&) = delete;

    PIDLIST_ABSOLUTE* put() { return &ptr_; }
    PIDLIST_ABSOLUTE get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    PIDLIST_ABSOLUTE ptr_ = nullptr;
};

std::string DisplayNameOf(IShellFolder* folder, PCUITEMID_CHILD child, SHGDNF flags) {
    STRRET value{};
    if (FAILED(GuardedDisplayName(folder, child, flags, &value))) return {};
    PWSTR text = nullptr;
    if (FAILED(::StrRetToStrW(&value, child, &text)) || !text) return {};
    std::string out = ToUtf8(text);
    ::CoTaskMemFree(text);
    return out;
}

// What the shell said, in the vocabulary the listing speaks.
//
// The COM facility carries a plain Win32 code often enough to be worth
// unwrapping - "the network is not reachable" reads very differently from
// "something went wrong", and it is the same distinction WinFileSystem draws.
shellhost::FolderStatus TranslateHResult(HRESULT hr) {
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        switch (HRESULT_CODE(hr)) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_NAME:
            case ERROR_BAD_NET_NAME:
                return shellhost::FolderStatus::NotFound;
            case ERROR_ACCESS_DENIED:
            case ERROR_LOGON_FAILURE:
                return shellhost::FolderStatus::AccessDenied;
            case ERROR_NOT_READY:
            case ERROR_BAD_NETPATH:
            case ERROR_NETWORK_UNREACHABLE:
            case ERROR_NO_NETWORK:
            case ERROR_DEVICE_NOT_CONNECTED:
                return shellhost::FolderStatus::Unavailable;
            default:
                break;
        }
    }
    if (hr == E_ACCESSDENIED) return shellhost::FolderStatus::AccessDenied;
    return shellhost::FolderStatus::Error;
}

// Size and modification time for an item that has a file behind it.
//
// Only for non-folders. Asking a drive root would put the enumeration of "PC"
// behind whatever a sleeping USB stick or a dropped network drive takes to
// answer, and both columns are blank for a directory anyway.
void FillFileDetails(const std::string& parsing, shellhost::FolderEntry& entry) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(ToWide(parsing).c_str(), GetFileExInfoStandard, &data)) return;
    entry.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

    ULARGE_INTEGER stamp;
    stamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
    stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
    if (stamp.QuadPart == 0) return;
    // FILETIME counts 100 ns ticks from 1601-01-01.
    entry.mtime = static_cast<int64_t>((stamp.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

// Parsing names come back from the shell and go out to Kite and back, and a
// path may return with a different case than it left with. ASCII folding is
// enough: what differs between the two spellings of a path is the drive letter
// and the separators, never the bytes of a non-ASCII name.
bool SameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char lhs = a[i];
        char rhs = b[i];
        if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<char>(lhs + 32);
        if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<char>(rhs + 32);
        if (lhs != rhs) return false;
    }
    return true;
}


// The two Recycle Bin columns. The SDK spells these as a format GUID plus a
// property id (ShlGuid.h) rather than as PKEY_* constants, so they are built
// here; they are the same pair Explorer shows as "Original location" and
// "Date deleted".
const PROPERTYKEY kDisplacedFrom = { PSGUID_DISPLACED, PID_DISPLACED_FROM };
const PROPERTYKEY kDisplacedDate = { PSGUID_DISPLACED, PID_DISPLACED_DATE };

// Reading those two columns, through IShellFolder2.
//
// GetDetailsEx hands back a VARIANT rather than a PROPVARIANT, so no propsys
// dependency is needed: "original location" arrives as a BSTR and "date
// deleted" as an OLE automation date, which only has to be compared, never
// formatted.
bool DetailString(IShellFolder2* folder, PCUITEMID_CHILD child, const PROPERTYKEY& key,
                  std::string& out) {
    VARIANT value{};
    ::VariantInit(&value);
    bool ok = false;
    if (SUCCEEDED(folder->GetDetailsEx(child, &key, &value)) && value.vt == VT_BSTR &&
        value.bstrVal) {
        out = ToUtf8(value.bstrVal);
        ok = true;
    }
    ::VariantClear(&value);
    return ok;
}

bool DetailDate(IShellFolder2* folder, PCUITEMID_CHILD child, const PROPERTYKEY& key,
                double& out) {
    VARIANT value{};
    ::VariantInit(&value);
    bool ok = false;
    if (SUCCEEDED(folder->GetDetailsEx(child, &key, &value)) && value.vt == VT_DATE) {
        out = value.date;
        ok = true;
    }
    ::VariantClear(&value);
    return ok;
}

// Does `candidate` name the same file as `target`?
//
// `candidate` is "original folder" + separator + "item name", and the name is
// what Explorer would print - which drops the extension when the user has asked
// for that. So a candidate that is the target minus a trailing ".something"
// counts too, as long as nothing but that extension is missing: the tail after
// the dot must not contain a separator, or "C:\a\b" would match "C:\a\b.c\d".
//
// No path splitting here on purpose. The host does not link kite_core (see the
// CMake comment on the target), and re-deriving path::Parent badly is a worse
// trade than comparing two strings the caller already knows the shape of.
bool NamesSameFile(const std::string& candidate, const std::string& target) {
    if (SameName(candidate, target)) return true;
    if (target.size() <= candidate.size() + 1) return false;
    if (!SameName(target.substr(0, candidate.size()), candidate)) return false;
    const std::string tail = target.substr(candidate.size());
    if (tail[0] != '.') return false;
    return tail.find_first_of("\\/") == std::string::npos;
}

}  // namespace

std::vector<PIDLIST_ABSOLUTE> ResolveItemsInFolder(
    const std::string& container, const std::vector<std::string>& parsingNames) {
    std::vector<PIDLIST_ABSOLUTE> found;
    if (container.empty() || parsingNames.empty()) return found;

    Pidl folderPidl;
    if (FAILED(::SHParseDisplayName(ToWide(container).c_str(), nullptr, folderPidl.put(), 0,
                                    nullptr)) ||
        !folderPidl) {
        return found;
    }

    ComPtr<IShellFolder> folder;
    if (FAILED(::SHBindToObject(nullptr, folderPidl.get(), nullptr, IID_IShellFolder,
                                folder.putVoid())) ||
        !folder) {
        return found;
    }

    const SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN;
    ComPtr<IEnumIDList> enumerator;
    if (FAILED(GuardedEnumObjects(folder.get(), flags, enumerator.put())) || !enumerator) {
        return found;
    }

    // Kept in the order asked for rather than the order enumerated: the caller
    // pairs these with its own selection, and a shell verb applied to a
    // different set than the one on screen is the worst kind of surprise.
    found.assign(parsingNames.size(), nullptr);
    size_t remaining = parsingNames.size();

    for (; remaining > 0;) {
        PITEMID_CHILD child = nullptr;
        if (GuardedNext(enumerator.get(), &child) != S_OK) break;

        const std::string parsing = DisplayNameOf(folder.get(), child, SHGDN_FORPARSING);
        for (size_t i = 0; i < parsingNames.size(); ++i) {
            if (found[i] || !SameName(parsing, parsingNames[i])) continue;
            // ILCombine copies both halves, so the child can be freed below
            // whether it matched or not.
            found[i] = ::ILCombine(folderPidl.get(), child);
            if (found[i]) --remaining;
            break;
        }
        ::CoTaskMemFree(child);
    }

    // Anything not found was deleted, restored or emptied between the listing
    // and now. Dropping the holes keeps the array dense for the caller, which
    // then acts on what still exists rather than on nothing at all.
    found.erase(std::remove(found.begin(), found.end(), nullptr), found.end());
    return found;
}


std::vector<PIDLIST_ABSOLUTE> ResolveTrashItemsByOrigin(
    const std::string& container, const std::vector<std::string>& originalPaths) {
    std::vector<PIDLIST_ABSOLUTE> found;
    if (container.empty() || originalPaths.empty()) return found;

    Pidl folderPidl;
    if (FAILED(::SHParseDisplayName(ToWide(container).c_str(), nullptr, folderPidl.put(), 0,
                                    nullptr)) ||
        !folderPidl) {
        return found;
    }

    // IShellFolder2, not IShellFolder: the columns live on the derived one, and
    // the Recycle Bin is where those two columns exist at all.
    ComPtr<IShellFolder2> folder;
    if (FAILED(::SHBindToObject(nullptr, folderPidl.get(), nullptr, IID_IShellFolder2,
                                folder.putVoid())) ||
        !folder) {
        return found;
    }

    const SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN;
    ComPtr<IEnumIDList> enumerator;
    if (FAILED(GuardedEnumObjects(folder.get(), flags, enumerator.put())) || !enumerator) {
        return found;
    }

    // One slot per requested path, holding the newest match seen so far. The
    // whole bin is walked rather than stopping at the first hit: the same path
    // can be in there several times over, and Ctrl+Z means the last of them.
    found.assign(originalPaths.size(), nullptr);
    std::vector<double> deletedAt(originalPaths.size(), 0.0);

    for (;;) {
        PITEMID_CHILD child = nullptr;
        if (GuardedNext(enumerator.get(), &child) != S_OK) break;

        std::string origin;
        if (DetailString(folder.get(), child, kDisplacedFrom, origin)) {
            std::string candidate = origin;
            if (!candidate.empty() && candidate.back() != '\\') candidate.push_back('\\');
            candidate += DisplayNameOf(folder.get(), child, SHGDN_INFOLDER);

            double when = 0.0;
            if (!DetailDate(folder.get(), child, kDisplacedDate, when)) when = 0.0;

            for (size_t i = 0; i < originalPaths.size(); ++i) {
                if (!NamesSameFile(candidate, originalPaths[i])) continue;
                if (found[i] && when <= deletedAt[i]) break;
                if (found[i]) ::CoTaskMemFree(found[i]);
                found[i] = ::ILCombine(folderPidl.get(), child);
                deletedAt[i] = when;
                break;
            }
        }
        ::CoTaskMemFree(child);
    }

    found.erase(std::remove(found.begin(), found.end(), nullptr), found.end());
    return found;
}

shellhost::ExtractResponse ExtractShellItem(const std::string& container,
                                            const std::string& parsingName) {
    shellhost::ExtractResponse response;
    if (parsingName.empty()) return response;

    // The item, pointed at the way everything else here points at one: through
    // its folder when there is one, because inside a namespace extension the
    // parsing name alone can resolve to something else entirely.
    Pidl parsed;
    PIDLIST_ABSOLUTE item = nullptr;
    std::vector<PIDLIST_ABSOLUTE> resolved;
    if (container.empty()) {
        if (FAILED(::SHParseDisplayName(ToWide(parsingName).c_str(), nullptr, parsed.put(), 0,
                                        nullptr)) ||
            !parsed) {
            return response;
        }
        item = parsed.get();
    } else {
        resolved = ResolveItemsInFolder(container, { parsingName });
        if (resolved.empty()) return response;
        item = resolved[0];
    }

    ComPtr<IShellItem> source;
    HRESULT hr = ::SHCreateItemFromIDList(item, IID_IShellItem, source.putVoid());
    for (PIDLIST_ABSOLUTE p : resolved) ::CoTaskMemFree(p);
    if (FAILED(hr) || !source) return response;

    // A fresh folder every time. Reusing one would mean deciding what to do
    // when the name is already there, and the honest answers - overwrite a file
    // something may still have open, or invent a second name - are both worse
    // than a directory that costs nothing.
    wchar_t temp[MAX_PATH] = L"";
    if (!::GetTempPathW(MAX_PATH, temp)) return response;
    GUID id{};
    if (FAILED(::CoCreateGuid(&id))) return response;
    wchar_t destination[MAX_PATH];
    if (::swprintf_s(destination, L"%sKite\\%08lX%04X%04X", temp, id.Data1, id.Data2, id.Data3) <
        0) {
        return response;
    }
    if (::SHCreateDirectoryExW(nullptr, destination, nullptr) != ERROR_SUCCESS) return response;

    ComPtr<IShellItem> folder;
    if (FAILED(::SHCreateItemFromParsingName(destination, nullptr, IID_IShellItem,
                                             folder.putVoid())) ||
        !folder) {
        return response;
    }

    ComPtr<IFileOperation> operation;
    if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation,
                                  operation.putVoid())) ||
        !operation) {
        return response;
    }
    // No UI of any kind: this is one file on a double click, and a progress
    // window that flashes past is not worth the chance of one that waits for an
    // answer nobody is looking at.
    operation.get()->SetOperationFlags(FOF_NO_UI | FOFX_NOCOPYHOOKS);
    if (FAILED(operation.get()->CopyItem(source.get(), folder.get(), nullptr, nullptr))) {
        return response;
    }
    if (FAILED(GuardedPerform(operation.get()))) return response;
    BOOL aborted = FALSE;
    if (FAILED(operation.get()->GetAnyOperationsAborted(&aborted)) || aborted) return response;

    // The name it landed under. Asked of the source rather than assumed from
    // the parsing name: what the folder calls a child is the folder's business.
    PWSTR leaf = nullptr;
    if (FAILED(source.get()->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &leaf)) || !leaf) {
        return response;
    }
    std::wstring full = std::wstring(destination) + L"\\" + leaf;
    ::CoTaskMemFree(leaf);

    // Believing the copy without looking is how an empty file gets opened.
    if (::GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return response;

    response.ok = true;
    response.path = ToUtf8(full.c_str());
    return response;
}

shellhost::FolderResponse EnumerateShellFolder(const std::string& parsingName) {
    shellhost::FolderResponse response;
    if (parsingName.empty()) {
        response.status = shellhost::FolderStatus::NotFound;
        return response;
    }

    Pidl absolute;
    HRESULT hr =
        ::SHParseDisplayName(ToWide(parsingName).c_str(), nullptr, absolute.put(), 0, nullptr);
    if (FAILED(hr) || !absolute) {
        response.status = TranslateHResult(hr);
        return response;
    }

    ComPtr<IShellFolder> folder;
    hr = ::SHBindToObject(nullptr, absolute.get(), nullptr, IID_IShellFolder, folder.putVoid());
    if (FAILED(hr) || !folder) {
        response.status = TranslateHResult(hr);
        return response;
    }

    // The folder's own name, so the tab and the breadcrumb can say "Recycle Bin"
    // rather than the CLSID that got us here. Kite overrides this for the three
    // places it has words of its own for; anything else has no other source.
    PWSTR title = nullptr;
    if (SUCCEEDED(::SHGetNameFromIDList(absolute.get(), SIGDN_NORMALDISPLAY, &title)) && title) {
        response.title = ToUtf8(title);
        ::CoTaskMemFree(title);
    }

    // INCLUDEHIDDEN mirrors the real-filesystem path: Kite's own "show hidden"
    // setting decides what reaches the screen, so the listing has to carry the
    // hidden ones for it to have anything to reveal.
    const SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN;
    ComPtr<IEnumIDList> enumerator;
    hr = GuardedEnumObjects(folder.get(), flags, enumerator.put());
    if (FAILED(hr) || !enumerator) {
        response.status = TranslateHResult(hr);
        return response;
    }

    for (;;) {
        PITEMID_CHILD child = nullptr;
        const HRESULT step = GuardedNext(enumerator.get(), &child);
        if (step != S_OK) {
            // Anything but "that was the last one" means the walk was cut short,
            // and a half listing presented as whole reads as "the rest was
            // deleted" - the same rule WinFileSystem::List follows.
            if (FAILED(step)) {
                response.entries.clear();
                response.status = TranslateHResult(step);
                return response;
            }
            break;
        }

        shellhost::FolderEntry entry;
        entry.name = DisplayNameOf(folder.get(), child, SHGDN_INFOLDER);
        entry.parsing = DisplayNameOf(folder.get(), child, SHGDN_FORPARSING);

        SFGAOF attrs = SFGAO_FOLDER | SFGAO_HIDDEN | SFGAO_FILESYSTEM | SFGAO_LINK;
        if (FAILED(GuardedAttributes(folder.get(), child, &attrs))) attrs = 0;
        ::CoTaskMemFree(child);

        // Without a parsing name there is nothing the row could point at, and a
        // row that cannot be opened, copied or asked about is worse than absent.
        if (entry.name.empty() || entry.parsing.empty()) continue;

        using Bit = shellhost::FolderAttr;
        if (attrs & SFGAO_FOLDER) entry.attrs |= static_cast<uint32_t>(Bit::Directory);
        if (attrs & SFGAO_HIDDEN) entry.attrs |= static_cast<uint32_t>(Bit::Hidden);
        if (attrs & SFGAO_LINK) entry.attrs |= static_cast<uint32_t>(Bit::Link);
        if (attrs & SFGAO_FILESYSTEM) {
            entry.attrs |= static_cast<uint32_t>(Bit::FileSystem);
            if (!(attrs & SFGAO_FOLDER)) FillFileDetails(entry.parsing, entry);
        }

        response.entries.push_back(std::move(entry));
        if (response.entries.size() > shellhost::kMaxFolderEntries) {
            response.entries.clear();
            response.status = shellhost::FolderStatus::Error;
            return response;
        }
    }

    response.status = shellhost::FolderStatus::Ok;
    return response;
}

}  // namespace kite::win
