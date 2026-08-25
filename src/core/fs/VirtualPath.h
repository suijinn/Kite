/// @file
/// @brief 実ファイルシステムに無い場所（「PC」「ごみ箱」「ネットワーク」）の表記。
///
/// この層は OS のシェル名前空間を知らない。知っているのは「`virtual:` で始まる
/// パスはファイルシステムのパスではない」という**文字列の規則**だけで、その先を
/// 何と読み替えるかはプラットフォーム層の仕事（Windows なら `platform/win/
/// VirtualNames.h` がシェルの解析名へ訳す）。
///
/// 規則をここに置くのは `config::Choose()` や `path::ToExtended()` と同じ理由 ─
/// Windows 実装の中に埋めるとテストできなくなる。

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kite::vfs {

/// @brief 仮想パスであることを示す前置。
///
/// ファイルシステムのパスとぶつからない綴りであることが唯一の要件。ドライブ文字
/// （`C:`）とも UNC（`\\srv`）とも、シェルの解析名（`::{CLSID}`）とも重ならない。
inline constexpr char kPrefix[] = "virtual:";

/// @brief 「PC」。ドライブと既知フォルダが並ぶ。
inline constexpr char kComputer[] = "virtual:computer";

/// @brief 「ごみ箱」。
inline constexpr char kRecycleBin[] = "virtual:recycle-bin";

/// @brief 「ネットワーク」。
inline constexpr char kNetwork[] = "virtual:network";

/// @brief 仮想パスかを判定する。
/// @param[in] p 対象のパス
/// @return `virtual:` で始まっていれば true
/// @note ここが true のパスは `IFileSystem` の実 FS 経路では読めない。列挙も
///       アイコンもプラットフォーム層がシェルへ回す
bool IsVirtual(std::string_view p);

/// @brief Kite が名前を知っている 3 つの根のどれかかを判定する。
/// @param[in] p 対象のパス
/// @return `kComputer` `kRecycleBin` `kNetwork` のいずれかなら true
bool IsWellKnown(std::string_view p);

/// @brief 表示名の i18n キーを返す。
/// @param[in] p 対象のパス
/// @return "ui.vfolder_computer" などのキー。名前を知らない場所なら nullptr
/// @note 3 つの根だけは**シェルの表示名ではなく Kite の言語設定に従わせる**。
///       Windows が英語で Kite が日本語、という組み合わせは普通にある
const char* LabelKey(std::string_view p);

/// @brief 書庫（フォルダとして開ける圧縮ファイル）の拡張子かを判定する。
/// @param[in] ext 先頭のドットを含まない拡張子。小文字で渡すこと
/// @return この環境のシェルがフォルダとして開ける書庫の拡張子なら true
/// @note 答えるのは **その OS が実際に開けるもの**だけ。開けない拡張子をここで
///       真にすると、開いた先に列挙する者が居らず「開いたのに空」になる ─
///       だから一覧は決め打ちではなく SetArchiveExtensions() で渡される
/// @note 二重拡張子は末尾だけで足りる。`path::Extension()` が返すのは `gz` で、
///       それを開けるシェルは中の tar もそのまま展開して見せる
bool IsArchiveExtension(std::string_view ext);

/// @brief この環境でフォルダとして開ける拡張子の一覧を差し替える。
/// @param[in] extensions 先頭のドットを含まない拡張子。小文字で渡すこと
/// @note **どれが開けるかは OS と、その利用者の関連付け次第**（Windows 11 は tar・
///       gz・7z・rar まで開けるが、Windows 10 は zip と cab だけ。7-Zip を入れて
///       関連付ければ 7z はシェル名前空間から外れる）。core が持てるのは規則
///       だけなので、実際に開ける一覧はプラットフォーム層が起動時に渡す
/// @note 既定は zip と cab ─ **Windows XP 以降どの版でも開ける 2 つ**で、渡されな
///       かったとき（テスト、一覧を訊けなかった環境）の答えになる
/// @note **起動時に 1 回だけ呼ぶこと。** 列挙のワーカーからも UI スレッドからも
///       読まれるので、走り始めた後に書き換えてはならない
void SetArchiveExtensions(std::vector<std::string> extensions);

/// @brief パスの末尾が書庫を名指しているかを判定する。
/// @param[in] p 対象のパス
/// @return 拡張子が書庫のものなら true
/// @note 見るのは拡張子だけ。実在するか、中身が本当に書庫かは答えない ─ 解決には
///       ディスクへの問い合わせが要るので、ショートカット（.lnk）と同じく拡張子で
///       先に振るう
bool IsArchiveName(std::string_view p);

/// @brief 実ファイルシステム上の書庫を「中を見る場所」として指す仮想パスを作る。
/// @param[in] file 書庫ファイルのパス
/// @return 前置を付けたパス。`file` が空なら空文字列
/// @note 中身を列挙するのはシェル名前空間なので、書庫の中は実 FS ではない ─
///       前置が付いた時点で、書き込みも監視も補完も自動的に外れる
std::string ArchivePath(std::string_view file);

/// @brief 書庫の中（または書庫そのもの）を指すパスから、書庫ファイルの実パスを返す。
/// @param[in] p 対象のパス
/// @return 書庫ファイルの実 FS 上のパス。書庫と関わりの無いパスなら空文字列
/// @note 入れ子の書庫では**外側**が答え。書庫の中に入った時点でそこから先は
///       シェルの領分で、内側の書庫は実 FS 上のファイルではない
std::string ArchiveFileOf(std::string_view p);

/// @brief 「..」で上がる先を返す。
/// @param[in] p 対象のパス
/// @return 親のパス。これ以上遡れないなら空文字列
/// @note 実 FS の根から仮想フォルダへ抜ける規則もここが持つ ─ `C:\\` の上は
///       「PC」、`\\server` の上は「ネットワーク」。共有の親をサーバーにしたのと
///       同じ判断で、その上に実在の置き場所がある以上、そこで止めると `..` も
///       `Alt+↑` もパンくずも辿り着けない場所になる
/// @note 書庫そのものの上は、それが置かれている**実フォルダ**。中の項目は仮想パスの
///       まま 1 つ戻る ─ 書庫の縁が、シェル名前空間と実 FS の境目になる
std::string ParentOf(std::string_view p);

/// @brief コマンドラインで渡された 1 つを、Kite が開ける形に直す。
/// @param[in] arg 受け取った文字列
/// @return 解析名（`::{CLSID}` で始まるもの）なら `virtual:` を付けたもの。
///         それ以外は `arg` のまま
/// @note **既定のファイルマネージャーとして登録すると、これが要る。** シェルは
///       フォルダらしきものすべてを Kite に回すようになるが、シェル自身の場所
///       （コントロール パネル、PC、ネットワークの場所）には渡せるパスが無く、
///       代わりに解析名が引数として届く ─ どのファイルシステム呼び出しも答えられない
///       文字列で、そのまま開けば「場所が利用できません」になる
/// @note 直すと言っても前置を足すだけ。解析名は**仮想パスの中身そのもの**なので、
///       付けた時点でシェル名前空間の列挙（`ShellFolder.cpp`）にそのまま乗る
std::string FromCommandLine(std::string_view arg);

/// @brief 名前を知らない仮想パスの、見出しに使う末尾要素を返す。
/// @param[in] p 対象のパス
/// @return 末尾の構成要素。前置しか無ければ空文字列
/// @note 3 つの根には LabelKey() を先に当てること。ここが答えるのは、シェルが
///       返した解析名（`::{CLSID}\\...`）で表される入れ子の仮想フォルダだけ
std::string TrailingName(std::string_view p);

}  // namespace kite::vfs
