// Kite - the narrow set of free functions every platform backend must provide.
// Everything else crosses the OS boundary through the interfaces in
// core/fs/FileSystem.h, core/app/Host.h and ui/Renderer.h.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kite::plat {

bool ReadTextFile(const std::string& utf8Path, std::string& out);
bool WriteTextFile(const std::string& utf8Path, std::string_view data);
bool EnsureDirectory(const std::string& utf8Path);

// Milliseconds since an arbitrary fixed origin; monotonic.
uint64_t NowMs();

// BCP-47-ish UI language of the current user, e.g. "ja" or "en".
std::string PreferredLanguage();

}  // namespace kite::plat
