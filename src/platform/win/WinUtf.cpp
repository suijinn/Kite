#include "platform/win/WinUtf.h"

#include <windows.h>

#include "core/base/PathUtil.h"

namespace kite::win {

std::wstring ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

std::string ToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), need,
                          nullptr, nullptr);
    return out;
}

std::string ToUtf8(const wchar_t* wide) {
    return wide ? ToUtf8(std::wstring_view(wide)) : std::string();
}

std::wstring ToExtendedPath(std::string_view utf8) {
    std::wstring w = ToWide(utf8);
    if (w.size() < 240) return w;
    if (w.compare(0, 4, L"\\\\?\\") == 0) return w;
    if (w.size() >= 2 && w[0] == L'\\' && w[1] == L'\\') {
        return L"\\\\?\\UNC\\" + w.substr(2);  // \\server\share -> \\?\UNC\server\share
    }
    if (w.size() >= 3 && w[1] == L':') return L"\\\\?\\" + w;
    return w;
}

}  // namespace kite::win
