# 版番号とコミットを、ビルドツリーの生成物へ焼き込む。
#
#   src/core/base/Version.h.in    -> generated/core/base/Version.h  （画面に出す表記）
#   src/platform/win/Version.rc.in -> generated/win/kite.rc          （exe のプロパティ）
#                                  -> generated/win/kite_shellhost.rc
#
# 3 つを 1 回の実行でまとめて書くのは git を何度も起動しないため。ビルドのたびに
# 走るので、ここでの数十ミリ秒は毎回の待ち時間になる。
#
# 設定時ではなくビルド手順として走らせる理由: コミットは commit のたびに動くが、
# CMakeLists.txt は動かない。再構成のときだけ書き直していたら、その日たまたま
# チェックアウトしていたコミットを名乗り続けることになる ─ 不具合報告では無意味を
# 通り越して有害。
#
# configure_file は内容が同じなら出力に触らないので、これで毎回全体が再コンパイル
# されることはない。
#
# コマンドラインで受け取るもの: KITE_VERSION, KITE_VERSION_MAJOR,
# KITE_VERSION_MINOR, KITE_VERSION_PATCH, KITE_SOURCE_DIR, KITE_HEADER_IN,
# KITE_HEADER_OUT, KITE_RC_IN, KITE_APP_RC_OUT, KITE_HOST_RC_OUT, KITE_ICON。

set(KITE_GIT_COMMIT "unknown")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${KITE_SOURCE_DIR}
        OUTPUT_VARIABLE commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE commitStatus)

    if(commitStatus EQUAL 0 AND commit)
        set(KITE_GIT_COMMIT ${commit})

        # Most builds people report problems with are not exactly any commit.
        # Untracked files do not count: they cannot change what was compiled.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${KITE_SOURCE_DIR}
            OUTPUT_VARIABLE modified
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE modifiedStatus)
        if(modifiedStatus EQUAL 0 AND NOT modified STREQUAL "")
            set(KITE_GIT_COMMIT "${KITE_GIT_COMMIT}+")
        endif()
    endif()
endif()

configure_file(${KITE_HEADER_IN} ${KITE_HEADER_OUT} @ONLY)

# アイコンを持つのは kite.exe だけ。ホストが画面に出すのはメニューだけで、
# ウィンドウは隠してある。
set(KITE_RC_NAME "kite")
set(KITE_RC_DESCRIPTION "Kite File Manager")
set(KITE_RC_ICON "IDI_APPICON ICON \"${KITE_ICON}\"")
configure_file(${KITE_RC_IN} ${KITE_APP_RC_OUT} @ONLY)

# 説明を分けているのは、応答しなくなったホストをタスクマネージャーで見つける唯一の
# 手掛かりがこの行だから。両方 "Kite" にすると、どちらを終了させればよいのか
# 画面から読み取れない。
set(KITE_RC_NAME "kite_shellhost")
set(KITE_RC_DESCRIPTION "Kite shell extension host")
set(KITE_RC_ICON "")
configure_file(${KITE_RC_IN} ${KITE_HOST_RC_OUT} @ONLY)
