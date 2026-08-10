/// @file
/// @brief 一覧で使う最小限のアイコン群。プリミティブだけで描く。
///
/// ファイルとフォルダの実アイコンはシェルから取る（`App::IconFor`）が、届くのは
/// 要求した数フレーム後になる。それまでの間と、`[ui] shell_icons = false` で
/// シェルを一切呼ばない設定にした場合に、ここのベクタ描画が使われる。
///
/// **代替であってプレースホルダではない。** 実アイコンと同じ大きさ・同じ位置に
/// 描くので、届いた瞬間に行がずれない。タブの × や + のように、シェルには
/// 対応するものが無いものも引き続きここが描く。

#pragma once

#include "ui/Renderer.h"

namespace kite::ui::glyph {

/// @brief フォルダのアイコンを描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形。中央の正方形に収まるよう調整される
/// @param[in] c 描画色
void Folder(Renderer& r, const RectF& box, const Color& c);

/// @brief ファイルのアイコンを描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void File(Renderer& r, const RectF& box, const Color& c);

/// @brief ドライブのアイコンを描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void Drive(Renderer& r, const RectF& box, const Color& c);

/// @brief 雲のアイコンを描く。クラウド上にのみ実体がある項目に使う。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void Cloud(Renderer& r, const RectF& box, const Color& c);

/// @brief 星のアイコンを描く。ブックマークに使う。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void Star(Renderer& r, const RectF& box, const Color& c);

/// @brief 下向きの三角を描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void ChevronDown(Renderer& r, const RectF& box, const Color& c);

/// @brief 上向きの三角を描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void ChevronUp(Renderer& r, const RectF& box, const Color& c);

/// @brief 右向きの三角を描く。パンくずの区切りに使う。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
void ChevronRight(Renderer& r, const RectF& box, const Color& c);

/// @brief 閉じるボタンの × を描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
/// @param[in] weight 線の太さ
void Cross(Renderer& r, const RectF& box, const Color& c, float weight);

/// @brief 追加ボタンの + を描く。
/// @param[in,out] r 描画先
/// @param[in] box 収める矩形
/// @param[in] c 描画色
/// @param[in] weight 線の太さ
void Plus(Renderer& r, const RectF& box, const Color& c, float weight);

}  // namespace kite::ui::glyph
