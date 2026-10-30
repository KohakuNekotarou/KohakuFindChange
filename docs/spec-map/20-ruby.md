# 20. ルビで探す・ルビを付ける（検索と置換の「Ruby」タブ）

> コード地図での位置: `model/KFCRubySearch.*`（タブが今のタブか・歩く・行の字・読みの読み書き）・`model/KFCReplaceEngine.cpp` の `WriteOwnTabRows`（行の Replace＝Table タブと共通）・`ui/KFCRubyTab.*`（タブの面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md`（R1〜R11）。各項目の末尾の（R<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/rb-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. タブと面

- **RUBY-01** ★本体の「検索と置換」のタブの行に、KFC の **「Ruby」タブ**が出る。そのタブが今のタブのとき、KFC の Find in … はルビの一覧を作る。閉じる時にそのタブなら閉じている間も同じ。（R1）回帰 `rb-face-shown`・`rb-face-find`・`rb-face-closed`・`rb-face-done`・`rb-face-switch-back`
  - 訂正:

- **RUBY-02** 面の中身＝**Search:**・**Find:**（Text With Ruby／Text Without Ruby）・**Base Text:**・**Ruby:**（Without のとき灰色）・**Change Ruby To:**・**Remove Ruby**・案内の2行。文字欄は1打鍵ごとに控える。値は KFC が覚える（起動している間だけ）。（R2）回帰 `rb-face-base`・`rb-face-reading`・`rb-face-kind`・`rb-face-change`・`rb-face-remove`・`rb-face-scope`
  - 訂正:

## 2. 探す

- **RUBY-10** ★**Text With Ruby**＝ルビの strand の連続が1行（`IRubyAttrStrand::GetRubyRun`）。Base Text は親文字、Ruby は読みに「含まれる」か（そのまま比べる）。空は何でも。（R3）回帰 `rb-any`・`rb-reading`・`rb-base`・`rb-both`・`rb-none`
  - 訂正:

- **RUBY-11** ★**Text Without Ruby**＝Base Text の出現のうちルビが掛かっていないもの。Base Text が空なら断る。（R3）回帰 `rb-without`・`rb-without-nobase`
  - 訂正:

- **RUBY-12** 見えない読みはルビでない（脚注の印が制御文字を読みとして受け継ぐ）。隠した条件の中は出さない。本文・セル・脚注・マスターの全部の物語。上限 1000。（R4・R5）回帰 `rb-footnote`・`rb-hidden`・`rb-cell`・`rb-book`・`rb-alldocs`
  - 訂正:

- **RUBY-13** 段の名前＝`[Ruby/Document] F:"漢字《かんじ》"`（Without は `F:"漢字 (no ruby)"`・無条件は `[Any Ruby]`）。保存したクエリにはならない。クエリのダイアログの Find・Run はタブを譲る。（R10）回帰 `rb-yield-closed`・`rb-query-run-scope`
  - 訂正:

- **RUBY-14** アプリケーションバーの欄は空になり、Return で一覧を作る。回帰 `rb-appbar-enter`
  - 訂正:

## 3. 行

- **RUBY-20** ★行は文字の行。一致の部分に `親文字《読み》`（長ければ切る＝親文字 20・読み 28 字）＝`Hit::runChain`。Without の行はふつうの字。着地・ページ・ロック・編集に付いていく・Copy・Clear はふつうの文字の行のまま。（R6）回帰 `rb-click`・`rb-next`・`rb-long`・`rb-copy`・`rb-clear`・`rb-edit-moved`・`rb-locked`・`rb-click-native-scope`
  - 訂正:

## 4. Replace

- **RUBY-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**。Change Ruby To＝読みを付ける（`kTARubyAttrBoss`・`kTARubyStringBoss` だけ＝ルビの見た目は触らない。新しく付ける複数文字はグループルビ）。Remove Ruby＝ルビの属性を全部外す（30 個・Change Ruby To より優先）。物語に strand が無ければ先に作る。（R7）回帰 `rb-change-one`・`rb-change-all`・`rb-without-apply`・`rb-no-strand`・`rb-remove`・`rb-remove-wins`・`rb-return`・`rb-cell-change`・`rb-book-replace`
  - 訂正:

- **RUBY-31** 断り＝Change 側に何も無い／既にそのとおり（数える＝書かない）。ロックされた行は灰色。（R7）回帰 `rb-nothing`・`rb-same`・`rb-locked`
  - 訂正:

- **RUBY-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Change Ruby`）。行は `Changed`＋書いた後の字（置換済みの灰色にはしない＝もう一度書ける）。取り消しで行の字も戻り、やり直しでまた `Changed`。（R8）回帰 `rb-undo`・`rb-undo-remove`
  - 訂正:

## 5. 効かない操作

- **RUBY-40** 灰色と扉は Table タブの TBL-40 と同じ。動く＝Copy・Clear・Search This Document Again・Search These Results Again（その一覧の探した条件で）。段から戻すと同じ行。Change All in Book は灰色。（R9・R10）回帰 `rb-menus`・`rb-search-again`・`rb-doc-again`・`rb-tier-back`・`rb-changeall-refused`
  - 訂正:

- **RUBY-41** 作っていないもの＝原稿の印（`｜漢字《かんじ》`）から付けて印を消す形・読みの GREP・ルビの見た目の検索と置換。（R11）
  - 訂正:
