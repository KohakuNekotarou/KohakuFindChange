# 24. スクリプトラベルを一覧にする・書き換える（Kohaku+ タブの Find: Script Label）

> コード地図での位置: `model/KFCScriptLabelSearch.*`（歩く・一致・行の字・書く口・面の Find Next／Change／Change All・クエリの実行）・`model/KFCReplaceEngine.cpp`（行の Replace＝`ReplaceLabelRows`・面の書き手＝`WriteFaceLabels`）・`ui/KFCKohakuTab.*`（面）。
> 設計書 `docs/superpowers/specs/2026-11-04-kfc-script-label-design.md`（S1〜S11）。回帰の表＝`work/kbs-regress/sl-cases.tsv`（材料＝`cases/sl-build.jsx`・表を作る台本＝`work/note-scripts/2026-11-04-kfc-script-label/make_sl_cases.py`）。
> 訂正の書き方は [00-index.md](00-index.md)。
> ⬜ **2026-11-04 現在**：一覧（SL-10〜SL-14）は実機で 14/14 PASS。面・クリック・Replace・面のボタン・クエリの試験は書いてあり、実機の回はまだ。

---

## 1. 面

- **SL-01** ★**どの言語でも Find: の最初に出る**（ラベルは言語に関わらない＝QR Code・Table と同じ）。日本語＝**Script Label**／QR Code／Table／Kenten／Ruby／Warichu（6つ）、他の言語＝**Script Label**／QR Code／Table（3つ）。種類の番号と一覧の位置は別（`KFCKohakuTab.cpp` の `kKindsInOrder`・`EntryOfKind`／`KindOfEntry`）。前の版が保存した並びの一覧は、開いたときに並びどおりに作り直す。起動直後に選ばれているのは今までどおり QR Code。回帰 `sl-face-entries`・`sl-face-entries-other`
  - 訂正: 2026-11-04 作者「Find の並びを Label QR Table にしておいてください。そのあと Kenten Ruby Wari に」＝それまでは最後（QR Code／Table／Ruby／Kenten／Warichu／Script Label）。

- **SL-02** Search: は QR Code と同じ2つ（All Documents／Document）。他の種類で Story 系を選んだまま来たら Document と見せる（KOHAKU-10）。回帰 `sl-face-entries`・`sl-face-title`
  - 訂正:

- **SL-03** その下に **Script Key:**（空＝全部のキー＝SL-11／名前＝スクリプトが `insertLabel` で付けたそのキーのラベル＝本体のパネルには出ない・`Label`＝本体のパネルのラベル）・**Match:**（Find Label: の読み方＝**Text (Contains)**（既定）／**Text (Whole Label)**／**GREP** の一覧・Find Label: の真上）・**Find Label:**（空＝そのキーのラベルを持つ物を全部）・**Change Label:**。**大文字と小文字は常に区別**（チェックは無い）。語はどの言語でも英語のまま（作者 11-04、日本語にする話の後で「では、英語のままで」）。欄は打った字のまま（前後の空白も切らない）。回帰 `sl-face-entries`・`sl-face-find`・`sl-face-grep`・`sl-face-whole`
  - 訂正: 2026-11-04 夜＝チェック3つ（Case Sensitive・Whole Label・GREP）を Match: の一覧＋常に区別に（作者「Grep の位置を Find の上に」「この３つをリストで選ぶとどうなります？」「大文字と小文字のチェックなくしましょうか、常に大文字と小文字は区別するでいいかも」「TEXT(Contains) とかがわかりやすいかも？」「では、りすとで」）。
  - 訂正: 2026-11-04 作者「Key ではわかりずらいので ScriptKey とかにしてほしい」＝欄の名前は `Key:` から `Script Key:` に。同じ日に「Case と Whole の横位置、Key: とかのリストの□の始まりの位置に合わせて欲しい」＝2つのチェックボックスは欄の左端から。

- **SL-04** 右の列＝Find in KFC・Find Next・Change・Change All・Change/Find（表と同じ並び）。Change All は押せる（空の Change Label: も「ラベルを外す」を頼んでいる）。Change と Change/Find は Find Next が見つけるまで灰色。回帰 `sl-face-entries`
  - 訂正:

- **SL-05** パネルの題＝`Kohaku Find/Change - Script Label - Document`（All Documents ならそう）。閉じて開き直すと種類と欄の字はそのまま。回帰 `sl-face-title`・`sl-face-closed`
  - 訂正:

## 2. 探す

- **SL-10** ★**文書の全部のページアイテム**＝全部のスプレッドと親スプレッド・ペーストボード・グループとその中・インライン／アンカー付きの物・枠に置いた画像（DOM の `allPageItems` と同じ集まり）。隠した物・ロックした物・隠した／ロックしたレイヤーの物も出す。回帰 `sl-all`・`sl-nested`・`sl-hidden-locked`
  - 訂正:

- **SL-11** 一致＝そのキーのラベルが空でなく、Find Label: が空か、Match: の読み方で合う＝**Text (Contains)**＝ラベルが Find Label: を含む／**Text (Whole Label)**＝ラベル全体が等しい／★**GREP**＝Find Label: を正規表現として探す（ECMAScript の文法＝`std::wregex`）、Change Label: では `$1`・`$&` で見つけた部分を使える。★**大文字と小文字は常に区別**（2026-11-04 夜から・全角半角・かなも畳まない）＝区別せずに探すなら GREP で `[Aa]`、GREP で全体一致なら `^…$`。正規表現でない式は「not a GREP expression」と言い、何も探さず一覧も消さない。★**Script Key: が空＝全部のキー**（本体のパネルのラベル＋`insertLabel` のキー全部＝`IScriptLabel::GetTags`）＝物ごと・キーごとに1行。`Label` と入れると本体のパネルのラベルだけ。回帰 `sl-find`・`sl-find-upper`・`sl-case`・`sl-whole`・`sl-whole-none`・`sl-key`・`sl-key-default`・`sl-key-label`・`sl-none`・`sl-grep-find`・`sl-grep-both-cases`・`sl-grep-anchored-case`（旧 `sl-grep-whole-case`）・`sl-grep-bad`・`sl-face-grep`・`sl-face-whole`
  - 訂正: 2026-11-04 作者「正規表現追加して欲しい」で GREP、「ScriptKey が空白ならぜんぶみつける？」で空＝全部のキーに（それまでは空＝本体のパネルのラベルだけ）。

- **SL-12** 段の名前＝`[Script Label/<範囲>] Find:"<字>" Change:"<字>"`（テキストのタブの段と同じく、探したときの Change Label:）。Find Label: が空なら `[Any Label]`、キーがあれば `Script Key:"<キー>" ` を前に。数の名詞＝`label(s)`（`4 label(s).`）。回帰 `sl-all`・`sl-key`
  - 訂正: 2026-11-04 作者「題名に Find しかない」＝`Change:"…"` を足した。キーの語も `Key:` から `Script Key:` に。

- **SL-13** Book Scope・All Documents は表と同じ（閉じた章も開いて読み、返す）。回帰 `sl-book`・`sl-alldocs`
  - 訂正:

- **SL-14** 上限は物の一覧と同じ 1000 行。Narrow Down は効かない（Kohaku+ の種類の一覧）。回帰 `sl-menus-grey`
  - 訂正:

## 3. 行

- **SL-20** ★**物の行**（QR Code の行と同じ）＝`ID<uid>:<レイヤーパネルの名前>  <ラベル>`。ページ順・場所の語（`grouped`・`inline`・`master`・`pasteboard`・`hidden`・`locked`）は物の行のもの。ラベルの改行は `¶`、タブは空白で描き（KFC のほかの行と同じ）、200字で切って `…`。★**Find Label: で見つけた字だけ濃く、ほかは薄く**（文字の行と同じ3つ＝前・一致・後。Find Label: が空ならラベル全体が濃い）。本体のパネル以外のキーのラベルは `<キー>: <ラベル>`。書いた後の行は新しいラベルを1つの字で描く。回帰 `sl-all`・`sl-nested`・`sl-hidden-locked`・`sl-multiline`・`sl-key-default`
  - 訂正: 2026-11-04 作者「検索結果の文字だけ濃く他の色がうすくならない」＝それまでは行全体が同じ濃さ。

- **SL-21** 行のクリック＝その物を選んでそのページへ（グループの子・インライン・画像も、その物自体を選ぶ）。ロック・非表示の物は選ばず、行の注に理由を出す。Next Result も同じ。回帰 `sl-click`・`sl-click-nested`・`sl-click-locked`・`sl-next-result`
  - 訂正:

- **SL-22** Search This Document Again＝一覧が探したときの条件（キー・字・Case・Whole）で探し直す。今の面の字ではない。回帰 `sl-search-again`
  - 訂正:

## 4. Replace（行の右クリック・Return）

- **SL-30** ★**書く値**＝一覧が探したときのキー・Find Label:・Match:（一覧の写し）で、**今の面の Change Label:**（テキストの行の Change To と同じ決まり）。Text (Whole Label) か Find Label: が空＝Change Label: そのもの／それ以外＝ラベルの中の Find Label: を全部 Change Label: に。空になればラベルは外れる（行は残り `Labeled`）。改行入りのラベルも改行のまま書き戻す。回帰 `sl-replace-one`・`sl-replace-all`・`sl-replace-whole`・`sl-replace-remove`・`sl-replace-multiline`
  - 訂正:

- **SL-31** 書く口＝`IScriptUtils::SetScriptingTag`（`IScriptTagCmdData.h` が言う公式の扉）。まとめて書いた行は Ctrl+Z 1回で戻り、行は `Labeled` を失う。やり直しで戻る。数 (R/N) に数える。回帰 `sl-replace-all`・`sl-undo`
  - 訂正:

- **SL-32** 書かない行と言葉＝既にそのとおり（`this row already reads as its tab asks.`・KFC が書いた後で字が合わなくなった行も）／探した後で誰かが書き換えた（`edited since`）／物が消えた（`this row's item is no longer in the document. Search again.`）／ロック（物かレイヤー・`this row is locked.`・**書く時点のロックで決める**＝探した後にロックした行も書かない・表の行と同じ。本体のパネルで書けるかは ⬜ 未実測）。回帰 `sl-already`・`sl-item-gone`・`sl-replace-locked-since`
  - 訂正: 2026-11-04 の再検査＝行の Replace と面の Change は探した時点のロックの印だけを見ていた（表の行・表の Change は書く時点で読む）。今は `KFCScriptLabelSearch::IsLockedNow` の1か所で読む（面の Change は `sl-btn-change-locked-since`）。
  - 訂正:

## 5. 面のボタン

- **SL-40** Find Next＝範囲（Search:）で前に見つけた物の次の一致の物を選ぶ（ページ順・最後の次は黙って最初へ）。★前に見つけた物は**位置**＝Change／Change/Find で書き換えて Find Label: に合わなくなっても、その次へ進む（最初へ戻らない）。面の下の行＝`<名前>  <ラベル> - page <ページ>`（QR Code の形）。無ければ本体の `Cannot find a match.`。回帰 `sl-btn-next`・`sl-btn-none`・`sl-btn-changefind-mid`
  - 訂正: 2026-11-04 の再検査＝前に見つけた物を一致の一覧の中だけで探していたので、書き換えた後の Find Next は最初の一致へ戻っていた（`sl-btn-changefind` は最初の物で試していて通っていた）。
  - 訂正:

- **SL-41** Change／Change/Find＝Find Next が見つけた物に今の面の Change 側。押せるのは、選択が Find Next の着地が残したとおりの間だけ（別の物を選ぶと灰色＝表の Change と同じ・選べなかった物＝ロック・非表示は、着地が残した選択のままなら押せる）。Change All＝範囲の一致の全部（ロックは残す）・Ctrl+Z 1回・アラート `Search is completed. N label(s) changed.`・面の行 `N of M label(s) changed.`。一覧の同じ物の行は `Labeled` になる（その行の章は今開いている文書をファイルで引く＝11-07 ブロック9の D9-c・表の TBL-66 と同じ直し・`WriteFaceLabels`）。回帰 `sl-btn-change`・`sl-btn-changefind`・`sl-btn-changeall`・`sl-btn-change-moved`
  - 訂正:

## 6. クエリ・効かない操作

- **SL-50** 保存（クエリの行の保存）＝`Script Label - <名前>.xml`（属性 `labelKey`・`labelFind`・`labelChange`・`labelWhole`・`labelGrep`＝Match:。`labelCase` は 11-04 夜まで＝今は読まない・`labelWhole` と `labelGrep` が両方立ったファイルは GREP として読む）。本体のクエリのメニューで選ぶと面がその条件になる。どの言語でも出る。回帰 `sl-q-save-roundtrip`・`sl-q-pick`
  - 訂正:

- **SL-51** Run Saved Queries の Run＝一覧を作らずに書く（ロックは残す）。★**Change Label: が空のクエリは書かない**（一覧だけ）＝走らせただけで全部のラベルが外れることはない。回帰 `sl-q-run`
  - 訂正:

- **SL-52** Change All in Book は灰色で、押しても断る（表の T12 と同じ文）。Search Text in These Objects も効かない。★**特殊操作とアウトライン作成は効く**（オブジェクトタブの行と同じく物に）＝InDesign でラベルを付け、一覧にし、まとめて変える使い方。回帰 `sl-changeall-book-refused`・`sl-menus-grey`・`sl-special-lock`
  - 訂正: 2026-11-04 作者「Script ラベルに何かつける、それを検索、そののち SP オペレーションとかで一括変化できそう」＝それまでは特殊操作・アウトライン作成も断っていた（行の Replace だけが書く）。

- **SL-53** 作らない（v1）＝ページアイテム以外のラベル（文書・ページ・スプレッド・レイヤー・ストーリー・表・セル・スタイル）／Search: の Story・Selection／パネルの中でラベルを直接書き換える欄。⬜**次の便（作者 11-04）**＝行の右クリックにオブジェクトスタイルの子メニュー（選んだスタイルを当てる）／Script Key: を一覧にする（[すべてのキー]・None＝本体のパネルのラベル・前面の文書の台本のキー）。
  - 訂正: 2026-11-04＝ラベルの正規表現（GREP）・全部のキー（Script Key: が空）・行の中の一致の濃さは作った（SL-11・SL-20）。
  - 訂正: 2026-11-07（ブロック7の不具合の再検査が見つけた・この章に書かれていなかった）＝**表とセルのラベルは 2026-11-05 から一覧にする**（設計書 `docs/superpowers/specs/2026-11-05-kfc-script-label-tables-cells-design.md` L1〜L11・`6094ce5`）＝表・セルの行は文字の行（物語の行の下）・ページアイテムの行は物の行（文書の行の直下）。★**1つの文書に両方があるときは、文書の行の下に物の行が先・物語の行が後**（行の番号の順＝検索はページの物を表より先に数える）＝それまでは物語の行だけが描かれ、ページアイテムの行はどこにも出なかった（文書の行の数には入っていた・Replace All Rows も書かなかった）。回帰 `slt-mixed-list`・`slt-mixed-replaceall`・`slt-mixed-selectall`（ROW-35・ROW-38 の訂正）
