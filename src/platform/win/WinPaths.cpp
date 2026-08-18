#include "platform/win/WinPaths.h"

#include <windows.h>

#include "core/base/PathUtil.h"
#include "platform/win/WinUtf.h"

namespace kite::win {

std::wstring ModuleFilePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        // Equal means the name was truncated to fit; only a shorter result is
        // known to be whole.
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ModuleDirectory() {
    std::wstring path = ModuleFilePath();
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash);
    return path;
}

std::wstring ToExtendedPath(std::string_view utf8) { return ToWide(path::ToExtended(utf8)); }

std::wstring QuoteArgument(std::wstring_view arg) {
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // A run of backslashes is literal unless a quote follows, where it is
        // halved - so double it there, and escape the quote itself as well.
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

}  // namespace kite::win
