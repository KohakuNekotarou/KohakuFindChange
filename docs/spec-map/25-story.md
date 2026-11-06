# 25. ストーリーを一覧にする（Kohaku+ タブの Find: Story・内部ストーリーも）

> コード地図での位置: `model/KFCStorySearch.*`（歩く・行の字・行の位置）・`model/KFCFaceText.cpp`（面の Find Next）・`model/KFCSearchEngine.cpp` の `LocateRow`（行はストーリーを追う）・`model/KFCResultModel.cpp` の `HangsOffItsDocument`・`GetHitRow`（行は文書の直下・行の頭の `ID<uid>: <最初の字>`＝2026-11-06）・`ui/KFCKohakuTab.*`（面）・`ui/KFCJump.cpp`（内部ストーリーの行は選ばない）・`ui/KFCRowScript.cpp` の `EditInStoryEditor`（Special Operations > Edit in Story Editor＝2026-11-06）。
> 設計書 `docs/superpowers/specs/2026-11-06-kfc-story-kind-design.md`（ST1〜ST14）。回帰の表＝`work/kbs-regress/st-cases.tsv`（作る台本＝`work/note-scripts/2026-11-06-kfc-story-kind/st_cases.py`）。
> 作者の依頼（2026-11-05）「あと検索タブにストーリーをつけてほしい。内部ストーリーも。内部。内部。」→「はい、ストーリータブに内部ストーリーも検索に含めて。」→「計画は貴方のおすすめで」。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 面

- **ST-01** ★**どの言語でも Find: に出る**＝4つ目（QR Code／Script Label／Spelling／**Story**／Table＝ABC 順・どの言語でも同じ位置＝KOHAKU-03）。回帰 `st-face-entries`・`st-face-entries-other`
  - 訂正: 2026-11-06 朝 作者「Kohaku+でのFindの並び QR ScriptLabel Story Table の順番に」＝それまでは最後（日本語は7つ目・他の言語は4つ目）。同じ日に Spelling を Script Label と Story の間に足した（SPELL-01）＝Story は4つ目。

- **ST-02** Search: は QR Code と同じ2つ（All Documents／Document）。他の種類で Story・To End of Story・Selection を選んだまま来たら Document と見せて探す（KOHAKU-11）。パネルの題＝`Kohaku Find/Change - Story - Document`。回帰 `st-face-entries`
  - 訂正:

- **ST-03** ★**欄もアイコンも無い**（条件が無い＝全部のストーリー）。右の列は Find Next と Find in KFC だけ（変えるものが無い＝QR Code・Warichu と同じ）。回帰 `st-face-columns`
  - 訂正:

## 2. 探す

- **ST-10** ★**文書のストーリーの一覧の全部を1つずつ行に**＝利用者のストーリーの後に**内部ストーリー**（`IStoryList` の `kNotUserAccessible`＝本体の検索と置換・スペルチェックの外）。★2026-11-05 の実測＝**どの文書にも内部ストーリーは1つ＝XML 構造の裏の保管場所**（文書を作ったときからある・枠が無い・配置していない XML 要素の文字が入る）。回帰 `st-find`・`st-xml`
  - 訂正:

- **ST-11** ★**何も外さない**＝親ページ・ペーストボード・隠したレイヤー・ロックしたレイヤー・あふれ・空のストーリーも並ぶ（位置の語＝文字の行と同じ＝`PA`・`PPB`・`hidden`・`locked`）。回帰 `st-master`・`st-pasteboard`・`st-hidden`・`st-locked`・`st-overset`・`st-empty`
  - 訂正:

- **ST-12** 段の名前＝`[Story/Document] Find:"[All Stories]"`。数の名詞＝`story(s)`（`3 story(s).`）。検索の後の文＝`Click a row to go to its story.`。All Documents・Book Scope は他の種類と同じ（閉じた章も開いて読み、返す）。上限＝物の一覧と同じ 1000 行。回帰 `st-find`・`st-alldocs`・`st-book`
  - 訂正:

## 3. 行

- **ST-20** ★**行＝ストーリーの事実**（表の行の `2x2 TS1` と同じ置き方）＝`1 frame, 6 characters`・`2 frames, 300 characters, overset`・`1 frame, 2 characters, 1 table, 1 footnote`・`text on path, 5 characters`・`endnotes, 1 frame, 3 characters`・`1 frame, empty`・内部は `internal: XML structure, empty`（字があれば `10 characters`）。上の枝＝ほかの文字の行と同じ `ID<uid>: <最初の字>`（XML の保管場所に字があればその字）。★**文字の数＝読む字**（段落の改行・表や脚注やアンカーの印・XML の印 U+FEFF は数えない・タブは数える）。枠は1つの枠を1つ（段組みの段は数えない）。あふれ＝本体の赤い＋と同じ判定（`ITextUtils::IsOverset`・最後の改行だけのあふれは言わない）。表＝表の種類の一覧に出る表（隠した条件・削除・ノートの中は数えない）。回帰 `st-find`・`st-facts`・`st-overset`・`st-path`・`st-endnote`・`st-empty`・`st-xml`
  - 訂正: 2026-11-06 作者「ドキュメント、その下にストーリーで終わってほしい。」「今はもう一つその下に子供が出ると思う。」「あくまでストーリーのみの検索ですので、シンプルに簡素な検索結果を表示します。」→ 示した形に「はい。」＝**ストーリーの枝の行をやめ、行は文書の直下に1段**＝`P1(1)  ID254: 一つ目の本文  [1 frame, 6 characters]`（位置の語・`ID<uid>: <最初の字>`・事実の順）。それまでは文書の下に枝 `ID<uid>: <最初の字>  (0/1)`、その下に行（3段）。回帰 `st-find`（`!^PROW branch\tID\d+:`）・Story の表の行の期待は全部この形

- **ST-21** 位置の語＝内部ストーリーは `internal`（`overset` と言わない＝枠を持たないのはあふれではない）。回帰 `st-find`
  - 訂正:

- **ST-22** ★**行のクリック＝そのストーリーの本文の全部を選ぶ**（そのストーリーの中で「すべてを選択」したのと同じ）。★**行は字でなくストーリーを追う**＝探した後に本文を書き換えても Missing にならず、その時の本文の全部を選ぶ。ストーリーが消えていれば Missing。ロック・隠したレイヤー・隠したスプレッド・あふれは文字の行の決まりのまま（JMP）。回帰 `st-click`・`st-click-grown`・`st-click-gone`
  - 訂正:

- **ST-23** ★**内部ストーリーの行のクリック＝文書を前に出し、何も選ばない**（選択は外す）＝行の注 `That story is InDesign's own - the XML structure's: it is on no page.`（XML の保管場所でなければ `That story is InDesign's own: it is on no page.`）。回帰 `st-click-internal`
  - 訂正:

- **ST-24** Search This Document Again は全部のストーリーを並べ直す。Search This Story Again・Narrow Down は効かない（Kohaku+ の種類の決まり）。回帰 `st-doc-again`・`st-menus-grey`
  - 訂正: 2026-11-06 ST-20 の訂正で**ストーリーの枝の行が無くなった**＝Search This Story Again を出す行そのものが無い（`st-menus-grey` からその手順と期待を外した）。

## 4. 書かない

- **ST-30** ★**書かない**＝行の Replace・Return は灰色。メニューを通らない道の扉＝`Replace: Find: Story lists the stories and changes nothing - a row is not replaced.`。Change All in Book は灰色・扉は `Change All: the Find/Change dialog is on Story on the Kohaku+ tab. This panel lists the stories there - nothing to change.`。回帰 `st-replace-grey`・`st-changeall-book-grey`
  - 訂正:

- **ST-31** ★**この版では Special Operations はストーリーの行に効かない**（Clear Overrides・Create Outlines・Tate-chu-yoko は灰色＝ストーリーの行は一致でない＝表・セルのラベルの行と同じ。Restore from Copy も灰色）。Clear from Results・Copy・目玉・Next／Previous Result はふつうの行と同じ。⬜次の候補（作者が 11-05 に気に入った案）＝Open in Story Editor・Apply Paragraph Style。回帰 `st-menus-grey`
  - 訂正: 2026-11-06 作者「ストーリータブでの検索結果できちんとスペシャルオペレーションも行えるように。」→ 案を聞いて「Open in Story Editor を足す」・使い方「ドキュメントを非表示で作る、表示する、その後ストーリーを検索する。ストーリーエディターで開ければドキュメントが非表示でストーリーだけを編集できて大変便利になりそう。」＝★**Special Operations > Edit in Story Editor**（本体の Edit > Edit in Story Editor と同じ語）を**ストーリーの行にだけ**出す（内部ストーリーの行・ほかの種類の行には出ない）。選んだ行ごとにそのストーリーをストーリーエディターで開く（DOM の `story.storyEdit()`＝Run Script と同じ行から物への道）＝★**窓なしで開いた文書でも開くのはストーリーエディターの窓だけ**（レイアウトの窓は開かない＝`layoutWindows 0`・`storyWindows 1`・11-06 実測）。文書は変えない＝取り消しの段は作らない。状態の文＝`Edit in Story Editor: opened N of M row(s).`（開けなかった行は理由ごとに `; N row(s) not found - search again`・`; N row(s) whose documents are not open`・`; N row(s) of InDesign's own (internal) stories, which open in no Story Editor`・最初の誤り）。Clear Overrides・Create Outlines・Tate-chu-yoko・Restore from Copy は灰色のまま。Copy（結果の写し）は CR で同じ日に無くなった。回帰 `st-se-menu`・`st-se-hidden`・`st-se-text-none`・`st-menus-grey`

- **ST-32** ストーリーを作った段を Ctrl+Z しても InDesign は固まらない（行のテキストフォーカスが自分で外れる＝UNDO-19）。回帰 `st-undo-story`
  - 訂正:

## 5. 面のボタンとクエリ

- **ST-40** 面の **Find Next**＝範囲のストーリーを一覧と同じ順で、選択のストーリーの次へ（最後の次は最初へ黙って戻る）。着地は ST-22／ST-23 と同じ。面の1行目＝`<最初の字> (1 frame, 6 characters) - page 1`（内部は `internal: XML structure, empty - internal`・2行目に ST-23 の文）。**Find in KFC**＝パネルの Find を Story で。回帰 `st-face-next`・`st-findinkfc`
  - 訂正:

- **ST-41** クエリ＝`Story - <名前>.xml`（種類だけ・条件なし）。本体のクエリの欄で選ぶ＝Kohaku+ タブが Story で開く。保存ボタンも同じ。KFC のクエリの窓の Run＝書くものが無いので飛ばして言う（`nothing to change to`）。どの言語でも出る。回帰 `st-q-pick`・`st-q-save-roundtrip`・`st-q-run`
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- XML の保管場所でない内部ストーリー（InCopy の割り当て・他のプラグインが作るもの）は実物で並べていない（文は `internal, <n> characters`・注は `That story is InDesign's own: it is on no page.` の見込み）。
- 大きな文書（数千のストーリー）での速さ（あふれを読むため、組まれていないストーリーは最後の枠まで組む）。
- 縦組みの文書の行・クリックを実機の絵では見ていない。
- 隠したスプレッドのストーリーの行・クリック（文字の行の決まりどおりの見込み）。
- パネルで行を Clear from Results した後の Ctrl+Z（`tb-clear-undo-story` の形）は Story では回していない（`st-undo-story` は一覧が立ったまま）。
- Edit in Story Editor（ST-31 の訂正）＝複数の行を選んで一度に開く・ブックの閉じた章の行（`whose documents are not open` の見込み）・探した後に消したストーリーの行（`not found` の見込み）・同じストーリーを2度開く（本体は前の窓を前に出す見込み）は実機で回していない（回したのは1行ずつ＝`st-se-hidden`）。
- ★**窓なしの文書だけが開いている間はパネルの Find が灰色**（KFC の Find は前面の文書が要る＝窓なしの文書は前面にならない）＝作者の使い方（非表示で作る→探す）は、**窓のある文書を1つ開いて All Documents で探す**と窓なしの文書のストーリーも並ぶ（`st-se-hidden` の形）。窓なしの文書だけのときに探せるようにするかは作者にまだ聞いていない。
