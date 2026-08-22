// 1 行のテキスト入力欄。Kite の入力欄 ─ パスや名前を尋ねる Prompt と、チューザの
// 絞り込み欄 ─ はどちらもこの上に載っているので、キャレットの数え方が壊れると
// アドレスバーとコマンドパレットの両方が同時に壊れる。
#include "TestFramework.h"
#include "core/input/TextField.h"

using namespace kite;

namespace {

TextField Typed(const std::string& text) {
    TextField f;
    f.text = text;
    f.SetCaret(text.size());
    return f;
}

}  // namespace

KITE_TEST(textfield, an_arrow_walks_one_character_at_a_time) {
    TextField f = Typed("abc");

    KITE_EXPECT(f.HandleKey(ParseChord("Left")) == TextField::Edit::Moved);
    KITE_EXPECT_EQ(f.caret, size_t(2));
    KITE_EXPECT_FALSE(f.hasSelection());

    f.HandleKey(ParseChord("Right"));
    KITE_EXPECT_EQ(f.caret, size_t(3));
    // 端で止まる。回り込むと、末尾の 1 文字を消そうとした手が先頭へ飛ぶ。
    f.HandleKey(ParseChord("Right"));
    KITE_EXPECT_EQ(f.caret, size_t(3));
}

// 多バイト文字の途中に着地しない ─ そこで切ると、描く側が文字を半分だけ measure する。
KITE_TEST(textfield, an_arrow_steps_over_a_whole_character) {
    TextField f = Typed("あい");
    KITE_EXPECT_EQ(f.text.size(), size_t(6));

    f.HandleKey(ParseChord("Left"));
    KITE_EXPECT_EQ(f.caret, size_t(3));
    f.HandleKey(ParseChord("Left"));
    KITE_EXPECT_EQ(f.caret, size_t(0));
}

// Ctrl が動く単位は «単語» ではなく «パスの構成要素»。この欄に入るのはパスなので、
// Users の途中で止まっても誰も嬉しくない。
KITE_TEST(textfield, ctrl_arrow_moves_by_path_component) {
    TextField f = Typed("C:\\Users\\hiroki");

    f.HandleKey(ParseChord("Ctrl+Left"));
    KITE_EXPECT_EQ(f.text.substr(f.caret), std::string("hiroki"));
    f.HandleKey(ParseChord("Ctrl+Left"));
    KITE_EXPECT_EQ(f.text.substr(f.caret), std::string("Users\\hiroki"));
}

// Shift は anchor を据え置く ─ ここを SetCaret にすると打鍵ごとに選択が作り直され、
// 伸ばせなくなる。
KITE_TEST(textfield, shift_arrow_grows_one_selection) {
    TextField f = Typed("abcd");

    f.HandleKey(ParseChord("Shift+Left"));
    f.HandleKey(ParseChord("Shift+Left"));
    KITE_EXPECT(f.hasSelection());
    KITE_EXPECT_EQ(f.Selection(), std::string("cd"));

    // 縮めれば返る。
    f.HandleKey(ParseChord("Shift+Right"));
    KITE_EXPECT_EQ(f.Selection(), std::string("d"));
}

// 修飾なしの矢印は選択を «食う» のではなく端へ畳む。
KITE_TEST(textfield, a_plain_arrow_collapses_the_selection) {
    TextField f = Typed("abcd");
    f.SelectRange(1, 3);

    f.HandleKey(ParseChord("Left"));
    KITE_EXPECT_FALSE(f.hasSelection());
    KITE_EXPECT_EQ(f.caret, size_t(1));

    f.SelectRange(1, 3);
    f.HandleKey(ParseChord("Right"));
    KITE_EXPECT_EQ(f.caret, size_t(3));
}

KITE_TEST(textfield, home_and_end_reach_the_edges_and_shift_selects_to_them) {
    TextField f = Typed("abcd");

    f.HandleKey(ParseChord("Home"));
    KITE_EXPECT_EQ(f.caret, size_t(0));
    f.HandleKey(ParseChord("Shift+End"));
    KITE_EXPECT_EQ(f.Selection(), std::string("abcd"));
}

KITE_TEST(textfield, ctrl_a_selects_everything) {
    TextField f = Typed("abcd");
    KITE_EXPECT(f.HandleKey(ParseChord("Ctrl+A")) == TextField::Edit::Moved);
    KITE_EXPECT_EQ(f.Selection(), std::string("abcd"));
    // 素の A は文字なので、この欄のキーではない（WM_CHAR がそれを入れる）。
    KITE_EXPECT(f.HandleKey(ParseChord("A")) == TextField::Edit::None);
}

// 選択したまま打った文字が置き換えではなく挿入になるなら、全選択に用は無い。
KITE_TEST(textfield, typing_over_a_selection_replaces_it) {
    TextField f = Typed("abcd");
    f.SelectAll();

    KITE_EXPECT(f.Insert("x"));
    KITE_EXPECT_EQ(f.text, std::string("x"));
    KITE_EXPECT_EQ(f.caret, size_t(1));
    KITE_EXPECT_FALSE(f.hasSelection());
}

KITE_TEST(textfield, backspace_and_delete_take_the_selection_first) {
    TextField f = Typed("abcd");
    f.SelectRange(1, 3);
    KITE_EXPECT(f.HandleKey(ParseChord("Backspace")) == TextField::Edit::Changed);
    KITE_EXPECT_EQ(f.text, std::string("ad"));

    f = Typed("abcd");
    f.SelectRange(1, 3);
    KITE_EXPECT(f.HandleKey(ParseChord("Delete")) == TextField::Edit::Changed);
    KITE_EXPECT_EQ(f.text, std::string("ad"));
}

KITE_TEST(textfield, backspace_and_delete_eat_one_character_when_nothing_is_selected) {
    TextField f = Typed("aあ");
    KITE_EXPECT(f.HandleKey(ParseChord("Backspace")) == TextField::Edit::Changed);
    KITE_EXPECT_EQ(f.text, std::string("a"));

    f.SetCaret(0);
    KITE_EXPECT(f.HandleKey(ParseChord("Delete")) == TextField::Edit::Changed);
    KITE_EXPECT(f.text.empty());

    // 端では何も起きない。打鍵は受け取ったので None ではない。
    KITE_EXPECT(f.HandleKey(ParseChord("Backspace")) == TextField::Edit::Moved);
    KITE_EXPECT(f.HandleKey(ParseChord("Delete")) == TextField::Edit::Moved);
}

// Shift+Delete は切り取りの別の綴り。クリップボードに触れないこの型は受け取らない。
KITE_TEST(textfield, shift_delete_is_left_to_whoever_holds_the_clipboard) {
    TextField f = Typed("abcd");
    f.SelectAll();
    KITE_EXPECT(f.HandleKey(ParseChord("Shift+Delete")) == TextField::Edit::None);
    KITE_EXPECT_EQ(f.text, std::string("abcd"));
}

KITE_TEST(textfield, clear_takes_the_caret_back_with_the_text) {
    TextField f = Typed("abcd");
    f.SelectAll();
    f.Clear();
    KITE_EXPECT(f.text.empty());
    KITE_EXPECT_EQ(f.caret, size_t(0));
    KITE_EXPECT_FALSE(f.hasSelection());
}
