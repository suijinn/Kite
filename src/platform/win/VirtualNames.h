/// @file
/// @brief `virtual:` 付きのパスと、シェルの解析名との相互変換。
///
/// core は「`virtual:` で始まるパスはファイルシステムのパスではない」ことしか
/// 知らない（`core/fs/VirtualPath.h`）。それが Windows のどの場所を指すのかを
/// 決めているのはここだけで、**ホストへ渡る文字列にはもう前置が付いていない** ─
/// `kite_shellhost.exe` はシェルの言葉しか知らないでよい。

#pragma once

#include <string>

namespace kite::win {

/// @brief 仮想パスをシェルの解析名に訳す。
/// @param[in] path Kite 側のパス
/// @return シェルに渡せる解析名。仮想パスでなければ空文字列
/// @note Kite が名前を知っている 3 つは CLSID の綴りに訳す。それ以外は前置を
///       外すだけ ─ 中身はもともとシェルが返してきた解析名だから
std::string ToShellParsingName(const std::string& path);

/// @brief シェルの解析名を Kite 側のパスに訳す。
/// @param[in] parsing シェルが返した解析名
/// @param[in] fileSystem 実ファイルシステム上の項目なら true
/// @return Kite 側のパス。`fileSystem` が true ならそのままのパス
/// @note 3 つの根に当たるものは `vfs::` の識別子に畳む。畳まないと、同じ場所を
///       指す綴りが 2 通りできて、タブの重複判定も設定の突き合わせも狂う
std::string FromShellParsingName(const std::string& parsing, bool fileSystem);

/// @brief シェルに渡せる形のパスに直す。
/// @param[in] path Kite 側のパス
/// @return 仮想パスなら解析名、そうでなければ `path` そのまま
/// @note **kite.exe からシェル（およびホスト）へ出ていく文字列は全部ここを
///       通す。** `virtual:` の付いた文字列がプロセスの外へ漏れると、受け取った
///       側はそれをパスとして解釈して「そんなファイルは無い」と答える
std::string ToShellPath(const std::string& path);

}  // namespace kite::win
