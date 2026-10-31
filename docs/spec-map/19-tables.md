# 19. 表スタイルで表を探す（Kohaku+ タブの Find: Table）

> コード地図での位置: `model/KFCTableSearch.*`（タブが今のタブか・歩く・行の字・書く）・`model/KFCReplaceEngine.cpp` の `WriteOwnTabRows`＋`TableWrite`（行の Replace）・`ui/KFCKohakuTab.*`（タブの面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md`（T1〜T13）。各項目の末尾の（T<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/tb-cases.tsv`。裁定＝`work/sdd/2026-10-31-kfc-table-tab/progress.md`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. タブと面

- **TBL-01** ★本体の「検索と置換」の **Kohaku+ タブ**（21 章）の Find: を **Table** にしているとき、KFC の Find in … は範囲の中の表を一覧にする。閉じる時にそうなら閉じている間も同じ。（T1・K1）回帰 `tb-face-shown`・`tb-face-find`・`tb-face-closed`・`tb-face-done`・`tb-face-switch-back`
  - 訂正:

- **TBL-02** 部品＝**Find Table Style:**（`[Any Table Style]`＋前面の文書の表スタイル）・**With Overrides Only**・**Change to Table Style:**（`[Leave As It Is]`＋前面の文書の表スタイル）・**Clear Overrides**・案内の2行。語の下に一覧（KOHAKU-20）。**Search:** は 5つ（KOHAKU-10）。値は KFC が覚える（起動している間だけ）。（T2・K5）回帰 `tb-face-pick`・`tb-face-overrides`・`tb-face-change`・`tb-face-clear`・`tb-face-scope`・`tb-part-face`
  - 訂正:

- **TBL-03** 一覧は**前面の文書に付いていく**（面を見せている間に前面の文書が変わる・表スタイルが増える・消える）。前面の文書に無い表スタイルを選んだままなら、一覧の末尾に残す（探すのは名前で全部の文書）。グループの中の表スタイルは `グループ:名前`（本体の区切りは U+E00B＝`KFCStylePathWords`）。（T2・T3）回帰 `tb-face-follows`・`tb-face-new-style`・`tb-face-asked-kept`・`tb-grouped`・`tb-grouped-any`
  - 訂正:

- **TBL-04** 面に Find Next は**無い**（設計の T13 を実装で取りやめた＝パネルの Find と Next Result で足りる・文字の範囲への着地は行のクリックの内部）。作者が欲しければ足す。
  - 訂正:

- **TBL-05** ~~タブが8枚で帯の両端が切れる~~ ⇒ 10-31 に Kohaku+ の1枚へまとめて解消（KOHAKU-01）。
  - 訂正:

## 2. 探す

- **TBL-10** ★**表スタイルは名前で比べる**（ブックの章は表スタイルを別々に持つ）。`With Overrides Only`＝`ITableStylesFacade::HasLocalOverrides`（表の上書き＝セルの上書きは数えない）。セルスタイルは探さない。（T3・T4）回帰 `tb-any`・`tb-style`・`tb-overrides`・`tb-overrides-style`・`tb-nomatch`・`tb-none`
  - 訂正:

- **TBL-11** 範囲＝`Search:`（Document／All Documents）か Book Scope。全部の物語（マスターの物語も）の全部の表（入れ子の表も）。検索と置換の Include のスイッチは見ない。行が `hidden`・`locked`・`overset` と言う。（T6）回帰 `tb-book`・`tb-alldocs`・`tb-nested`・`tb-master`・`tb-overset`・`tb-hidden-layer`・`tb-locked-row`
  - 訂正:

- **TBL-12** ★**条件で隠した文字・Track Changes の削除文字・ノートの中の表は出さない**（本体の検索も DOM も見ない）。見せている条件の中の表は出る。回帰 `tb-hidden-condition`・`tb-shown-condition`
  - 訂正:

- **TBL-13** 上限＝行 1000（物の一覧と同じ）。超えたら `Stopped at the 1000 limit.`。（T7）回帰 `tb-cap`
  - 訂正:

- **TBL-14** Book Scope で Search: が All Documents なら本体のタブと同じ文で断る（FIND-13 の形）。段の名前＝`[Table/Document] F:"TS1"`（With Overrides Only は ` +`・無条件は `[Any Table Style]`）。保存したクエリにはならない。クエリのダイアログの Find・Run は自分のタブを言う（タブは譲る＝Search: も本体のタブのもの）。回帰 `tb-yield-open`・`tb-yield-closed`・`tb-query-run-scope`・`tb-text-replace-name`
  - 訂正:

- **TBL-15** アプリケーションバーの欄は空になり、Return で一覧を作る（QR-22 と同じ）。回帰 `tb-appbar-enter`
  - 訂正:

## 3. 行

- **TBL-20** ★行は**文字の行**（表の文字＝`ITableTextContainer` の錨に立つ）。一致の部分に `2x2 <表スタイル>`（上書きがあれば ` +`）を描く＝`Hit::runChain`。ページ・ロック・非表示・overset・編集に付いていく・段の Copy／Clear はふつうの文字の行のまま。（T5）回帰 `tb-copy`・`tb-clear`・`tb-edit-moved`
  - 訂正:

- **TBL-21** クリックと Next Result＝表の文字を選ぶ（本体で文字として選んだのと同じ）。文字のタブと同じに本体のタブの Search: を守る。（T5）回帰 `tb-click`・`tb-next`・`tb-click-native-scope`
  - 訂正:

## 4. Replace

- **TBL-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**を表に当てる（文字の行の Change To と同じ）。Change to Table Style＝`ApplyTableStyle(style, table, clear, kFalse)`・Clear Overrides だけ＝`ClearLocalOverrides(table)`。表スタイルは**行の文書の中で名前で引く**。（T8）回帰 `tb-replace-style`・`tb-replace-one`・`tb-replace-clear`・`tb-replace-both`・`tb-return`・`tb-book-replace`・`tb-alldocs-replace`
  - 訂正:

- **TBL-31** 断り＝Change 側に何も無い／行の文書にその名前が無い（数える）／既にそのとおり（数える＝書かない）／消された表・ロックされた表。ロックされた行は灰色。（T8・T10）回帰 `tb-replace-nothing`・`tb-replace-nostyle`・`tb-same-style`・`tb-table-deleted`・`tb-locked-row`・`tb-locked-return`
  - 訂正:

- **TBL-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Apply Table Style`）。行は `Styled`＋書いた後の字。取り消しで行の字も戻り、やり直しでまた `Styled`。追跡＝`StepRecorder::RecordItem`＋`Fingerprint` は表だけ「表スタイルの UID＋上書きの有無」（バイト列は Redo で戻らない＝実測）。（T9）回帰 `tb-undo`・`tb-undo-clear`
  - 訂正:

## 5. 効かない操作

- **TBL-40** 灰色＝Narrow Down（全部）・Search Text in These Objects・行の Special Operations・Restore from Copy・物語の行の Search This Story Again。扉も断る（`RefuseOwnTabRow`）。動く＝Copy・Clear・Search This Document Again・Search These Results Again（**その一覧の探した条件で**＝面が変わっていても同じ）。段から戻すと同じ行。（T11）回帰 `tb-menus`・`tb-search-again`・`tb-doc-again`・`tb-tier-back`
  - 訂正:

- **TBL-41** Change All in Book はこのタブで灰色（扉も断る）。（T12）回帰 `tb-changeall-refused`
  - 訂正:

## 6. 表の中の文字を探す

- **TBL-50** ★Table の一覧の見出しの右クリック **Search Text in These Tables**＝一覧の表（Clear で外した行の表を除く）のセルの文字を、本体の検索と置換のタブ（Text／GREP／Glyph／Transliterate）の条件で探し、新しい段に出す（`[Text/Tables] F:"…"`）。元の表の一覧は段として残る。Table の一覧以外では灰色。Find/Change が Kohaku+ タブのままなら断る（`Find/Change is on the Kohaku+ tab - …`）。（K10）回帰 `tb-text`・`tb-text-grey`・`tb-text-on-kohaku`・`tb-text-none`・`tb-text-cleared`
  - 訂正:

- **TBL-51** 新しい段の行はふつうの文字の行＝Replace・Search These Results Again・Search This Story Again が効く（同じ表を、今の中身で）。Search This Document Again は灰色。表の中の表は一度だけ数える。消えた表は数えて言う。ブックの閉じた章は開いて歩いて閉じる。（K10）回帰 `tb-text-replace`・`tb-text-again`・`tb-text-story-again`・`tb-text-doc-grey`・`tb-text-nested`・`tb-text-nested-inner`・`tb-text-gone`・`tb-text-book`
  - 訂正:
