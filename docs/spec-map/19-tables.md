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

- **TBL-04** ★面の右上に**本体と同じ4つのボタン**（11-01 作者の試用「公式のテキスト検索と同様の」）＝7 章。
  - 訂正: 10-31 は「面に Find Next は無い」（設計の T13 を実装で取りやめた）だった＝作者が欲しいと言った。

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

- **TBL-14** Book Scope で Search: が All Documents なら本体のタブと同じ文で断る（FIND-13 の形）。段の名前＝`[Table/Document] Find:"TS1" Change:"TS2"`（With Overrides Only は Find の ` +`・無条件は `[Any Table Style]`。★2026-11-05 から **Change も**＝作者「テーブルタブで検索、結果の題名のところにChangeがない」・Script Label の見出しと同じ形＝Change to Table Style が空なら `[Leave As It Is]`・Clear Overrides にチェックがあれば後ろに ` Clear Overrides`＝`Find:"TS1" Change:"TS2" Clear Overrides`）。保存したクエリにはならない。クエリのダイアログの Find・Run は自分のタブを言う（タブは譲る＝Search: も本体のタブのもの）。回帰 `tb-yield-open`・`tb-yield-closed`・`tb-query-run-scope`・`tb-text-replace-name`・`tb-tier-change`・`tb-tier-leave`
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

- **TBL-50** ★Table の一覧の見出しの右クリック **Search Text in These Tables**＝一覧の表（Clear で外した行の表を除く）のセルの文字を、本体の検索と置換のタブ（Text／GREP／Glyph／Transliterate）の条件で探し、新しい段に出す（`[Text/Tables] Find:"…"`）。元の表の一覧は段として残る。Table の一覧以外では灰色。Find/Change が Kohaku+ タブのままなら断る（`Find/Change is on the Kohaku+ tab - …`）。（K10）回帰 `tb-text`・`tb-text-grey`・`tb-text-on-kohaku`・`tb-text-none`・`tb-text-cleared`
  - 訂正:

- **TBL-51** 新しい段の行はふつうの文字の行＝Replace・Search These Results Again・Search This Story Again が効く（同じ表を、今の中身で）。Search This Document Again は灰色。表の中の表は一度だけ数える。消えた表は数えて言う。ブックの閉じた章は開いて歩いて閉じる。（K10）回帰 `tb-text-replace`・`tb-text-again`・`tb-text-story-again`・`tb-text-doc-grey`・`tb-text-nested`・`tb-text-nested-inner`・`tb-text-gone`・`tb-text-book`
  - 訂正:

## 7. 面のボタン（作者 11-01 の試用＝`docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md` F1〜F9。本体の実測＝`docs/ai-notes/kfc-native-text-tab-measure-2026-11-01.md`。コード＝`model/KFCTableSearch.cpp` の `Next`・`ChangeFound`・`ChangeAll`・`KFCReplaceEngine::WriteFaceTables`・`ui/KFCJump.cpp` の `LandOnTable`・`ui/KFCKohakuTab.cpp` の `Table*`）

- **TBL-60** ★面の右の列に上から **Find Next／Change／Change All／Change/Find**＝本体と同じ 116×26・間隔5・面の上から 15／46／77／108（方向を付けない本体の Object タブの形）。★11-02 から**右の列は種類で共通**（KOHAKU-02＝Ruby・Kenten も同じボタン、QR Code は Find Next だけ）。`Find Table Style:` の語は Search: の下の行（Table のときだけ）。下に面の共通の2行（KOHAKU-37）。（F1・F2・F9・L1）回帰 `tb-btn-place`・`rb-face-columns`
  - 訂正:

- **TBL-61** ★**Find Next**＝`Search:` の範囲の表を一覧と同じ順に（文書ごと・ページ順）、押すたびに歩き直す。**どこから**（本体の Text タブの実測）＝選択が前の着地のままなら次の表／動かしていれば同じ物語の後ろの表→無ければその物語の最後の表の次→物語に表が無い・文字の選択が無いなら前に見つけた表の次→先頭。最後の次は黙って先頭。一致なしは本体の文のアラート `Cannot find a match.`。Book Scope は見ない（ダイアログのボタンはブックを知らない＝QR と同じ）。（F3）回帰 `tb-btn-next`・`tb-btn-moved`・`tb-btn-none`・`tb-btn-all-docs`
  - 訂正:

- **TBL-62** ★**範囲は選択が動いた時に読み直し、前の着地のままなら覚えたものを使う**（着地で選択が表に変わるので、毎回読むと Story や Selection が表1つに縮む）。今の選択で選べない範囲はドキュメント全体＋面の2行目で言う（KOHAKU-12 と同じ文）。（F4）回帰 `tb-btn-story`・`tb-btn-selection`
  - 訂正:

- **TBL-63** 着地＝行のクリックと同じ＝表の文字を選ぶ（Type ツール）。ロック＝見せて印、選ばない。非表示のスプレッド＝何も動かさない。overset・非表示のレイヤー＝選んで言う。理由は面の2行目。（F5）回帰 `tb-btn-locked`・`tb-btn-overset`・`tb-btn-hidden-layer`
  - 訂正:

- **TBL-64** ★**Change**＝見つけた表に Change 側（1回の Ctrl+Z）。押せるのは見つけた表を選んでいて Change 側に何かあるとき＝選択を動かすと灰色（`ActiveSelectionObserver`＝選択が前の着地のままかを読む）・当てた後も灰色（本体の Text タブと同じ）。**Change/Find**＝当てて次へ（当てられなくても次へ・理由は2行目）。既にそのとおり・文書にその名前が無い・ロックは面の行で言う。（F6）回帰 `tb-btn-grey`・`tb-btn-change`・`tb-btn-change-find`・`tb-btn-change-as-asked`・`tb-btn-doc-closed`
  - 訂正:

- **TBL-65** ★**Change All**＝範囲の条件に合う表全部に Change 側（全部で Ctrl+Z 1回）。アラート `Search is completed. N table(s) changed.`（本体の文に合わせる）。一致なしは `Cannot find a match.`。ロック・既にそのとおり・その名前が無い文書の表は飛ばし、面の行で数えて言う。押せるのは Change 側に何かあるとき。（F7）回帰 `tb-btn-change-all`・`tb-btn-change-all-style`
  - 訂正:

- **TBL-66** ★一覧に同じ表の行があれば、行も `Styled`＋書いた後の字になり Ctrl+Z で一緒に戻る（行の Replace と同じ＝一覧とページを食い違わせない）。**別の種類の一覧・一覧なしのときは一覧に触らない**（一覧を捨てない）。（F8）回帰 `tb-btn-rows-follow`
  - 訂正:

- **TBL-67** 別の種類で窓を開いてから Table にしても押せる（列の大きさを支柱で持つ＝KOHAKU-02 と同じ守り）。走っている最中は断り、面の1行目で言う。（Review Focus）回帰 `tb-btn-after-another-kind`

## 8. 表の行の一覧と Apply Cell Style（作者 11-05「テーブルを検索した後、ROWの一覧を作るのが欲しい。そのあとそのROWにSPオペレーションでセルスタイルを当てたい」＝設計書 `docs/superpowers/specs/2026-11-05-kfc-table-rows-cell-style-design.md` T1〜T11・コード＝`model/KFCTableRows.*`・`model/KFCReplaceEngine.cpp` の `kCellStyleOp`／`ApplyCellStyleRow`・`ui/KFCResultHistory.cpp` の `ListRowsOfTables`・`ui/KFCJump.cpp` の `TableOfRowAt`・`ui/KFCStyleMenu.*` の `kCellStyles`・回帰の表＝`work/kbs-regress/trw-cases.tsv`）

- **TBL-70** ★Table の一覧の見出しの右クリック **List Rows of These Tables**＝一覧の表（Clear で外した行の表を除く）の行を、表ごとに上から1行ずつ新しい段に出す（`[Table Rows/Document]`・元の表の一覧は段として残る）。Table の一覧以外では灰色。行の語＝`Row <n>[ (header)| (footer)]: <セル> | <セル>`（n は1から・セルは24字・行は100字で切る）。上限 1000 行。ブックの閉じた章は開いて読んで閉じる・ほかの一覧の閉じた文書は名前を言って飛ばす。（T1〜T4）回帰 `trw-list`・`trw-list-grey`
- **TBL-71** ★行は**文字の行**＝その行に左上のあるセルの文字の範囲（`GetTextChunk`・11-05 実測で1行1続き・縦の結合は上の行だけ・表の中の表の文字は外の表の範囲の外）＋`Hit::tableUID`。左上のセルが1つも無い行は載せずに数えて言う。（T3）回帰 `trw-merged`
- **TBL-72** ★行のクリック＝**その行のセルを選ぶ**（`ITableSelectionSuite::SelectRows`＝11-05 実測で文字の選択は最初のセルに縮むため・結合で覆われた行は覆うセルを除く＝本体と同じ）。（T5）回帰 `trw-click`
- **TBL-73** ★行の Special Operations の一番上 **Apply Cell Style ▸**＝前面の文書のセルスタイル（パネルの並び・`[なし]` も・グループは `G:S`）。当てると選んだ行のセルに**名前で**当てる（その名前の無い文書の行は断って数える）・上書きは残り・スタイルの段落スタイルも当たる（DOM の `appliedCellStyle =` と同じ＝11-05 実測）・取り消し1段 `Apply Cell Style`・行の語 `Styled`。当てる行は書く時に範囲の頭の文字のセルの行を読み直す（後から行を足しても元の行に当たる）。ロックの行は灰色。編集した行は「cells edited since」で断る（一覧を作り直す）。文字の一覧の行のこの子メニューは空。（T6・T7）回帰 `trw-style`・`trw-style-all`・`trw-style-undo`・`trw-style-group`・`trw-style-twin`・`trw-style-twodocs`・`trw-style-after-insert`・`trw-style-nested`・`trw-style-locked`・`trw-menu-text-list`
- **TBL-74** 表の行の一覧は検索が作らない（Run の一覧と同じ）＝Replace・Narrow Down・Search These Results Again・Search This Story／Document Again・List Rows of These Tables は灰色・Special Operations の Clear Overrides／Create Outlines／Tate-chu-yoko も灰色（行は一致ではない）。Copy Rows・Clear from Results・目玉は使える。（T8）回帰 `trw-list-again-grey`・`trw-replace-grey`・`trw-textops-grey`
  - 訂正:
