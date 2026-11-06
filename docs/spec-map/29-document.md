# 29. 開いている文書を一覧にする（Kohaku+ タブの Find: Document・窓なしの文書）

> コード地図での位置: `model/KFCDocumentSearch.*`（一覧の行＝文書・行の字）・`model/KFCSearchEngine.cpp`（振り分け・`ResolveRunTarget` の All Documents・`LocateRow`）・`model/KFCBookScope.cpp` の `HasScopeTarget`（W1）・`ui/KFCKohakuTab.*`（面・Open Hidden...）・`ui/KFCResultListAdapter.cpp`（行は段の直下）・`ui/KFCJump.cpp`（Document の行の着地・ブックの行の Open Without a Window）・`ui/KFCDocumentOps.*`（Show・Close・Save・Revert・Open Hidden...）。
> 設計書 `docs/superpowers/specs/2026-11-06-kfc-windowless-documents-design.md`（W1〜W5）・計画 `docs/superpowers/plans/2026-11-06-kfc-windowless-documents.md`。回帰の表＝`work/kbs-regress/doc-cases.tsv`・`wl-cases.tsv`（作る台本＝`work/note-scripts/2026-11-06-kfc-windowless/`）。
> 作者の依頼（2026-11-06 夕）「窓なしの文章でも検索できるようにしてほしい。」「検索にドキュメントタブというのを追加してほしい。」「これは窓なしのドキュメントを見つけるために検索したい。」「そのタブの中にドキュメントを窓なしで開くというのをつけてほしい。」「スペシャルオペレーションは閉じる。」「非表示のドキュメントなら、表示 というのをつけてほしい。」「表示されているドキュメントの場合は選択できない（灰色）」→ 案（Show・Close・Save・Revert・Run Script・Label）に「はい」。ブックの行の「窓なしで開く」は「ブックに対して検索したときに、ブックの行の右クリックに欲しい」。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 窓なしの文書を探す

- **DOC-01** ★**窓なしの文書だけが開いていても、Search: が All Documents ならパネルの Find が押せて走る**（W1）＝開いている文書（窓のあるなしを問わない・KFC が検索のために持っている章は除く）が1つでもあればよい。Document・Story・To End of Story・Selection は前面の文書が要る（前面の文書が無ければ灰色）。回帰 `wl-text-hidden-only`・`wl-story-hidden-only`・`wl-object-hidden-only`・`wl-doc-scope-grey`
  - 訂正:

## 2. Find: Document（面と一覧）

- **DOC-10** ★**Kohaku+ の Find: の一番上に Document**（どの言語でも・ABC 順＝KOHAKU-03）。面＝欄もアイコンも無い。Search: は **All Documents だけ**（前面の文書1つを並べても見つけることにならない＝私の判断）。右の列＝**Find in KFC** と **Open Hidden...**（Find Next・Change は無い＝窓なしの文書には着地できない）。回帰 `kh-kind-order`・`doc-openhidden`
  - 訂正:

- **DOC-11** **一覧＝開いている文書を1つ1行**、段（ブックならブックの行）の直下に並ぶ（文書の行の枝は作らない）。行の字＝`<名前>  [<窓>, <保存>]` と保存先 ` - <フルパス>`。窓＝`no window`／`1 window`／`<n> windows`（レイアウトの窓）、ストーリーエディターの窓があれば `, <n> Story Editor`（ストーリーエディターの窓だけなら `no layout window, 1 Story Editor`＝11-06 の再検査）。保存＝`not saved`／`modified`（保存済みで変更なしは書かない）。段の名前＝`[Document/All Documents] Find:"[All Documents]"`。Book Scope ON＝ブックの章のうち開いているものだけ（閉じた章は開かない・並べない）。メッセージ欄の要約＝`<n> document(s).` の後に ` Click a row to bring its document to the front (one with no window: Special Operations > Show).`。Show／Save の後は、その文書の行を作り直して今の窓と保存の状態を書く（Search This Document Again と同じ道）。回帰 `doc-list`・`doc-list-saved`・`doc-show`・`doc-save-modified`
  - 訂正: 11-06 の再検査＝①Book Scope で開いている章が1つも並ばなかった（章の `docRef` は空で渡る＝ファイルで聞き直す）②Show／Save の後も行が「no window」「modified」のままだった ③要約に窓なしの文書への道を足した

- **DOC-12** **行のクリック**＝その文書の窓を前に出す。**窓なしの文書は窓を開かない**＝メッセージ欄 `<名前> has no window - Special Operations > Show opens one.`（作者の Show を明示の操作にするため・私の判断）。回帰 `doc-click-hidden`
  - 訂正:

- **DOC-13** **面の Open Hidden...**＝選んだ InDesign の文書（`*.indd`）を窓なしで開く（KFC が章を開くのと同じ＝アラートも出さない）。開いた文書は利用者のもの。面の1行目＝`Opened <名前> without a window - Find lists it.`（一覧は作り直さない）。もう開いている文書は開き直さずにそう言う。回帰 `doc-openhidden`
  - 訂正:

## 3. Document の行の Special Operations

- **DOC-20** ★**Show**＝窓なしの文書にレイアウトの窓を開いて前に出す。**窓のある文書では灰色**（作者）。ストーリーエディターの窓だけの文書は窓なしと数える。回帰 `doc-show`・`doc-show-grey`
  - 訂正:

- **DOC-21** **Close**＝本体の閉じる（変更のある文書は本体の「保存しますか」が出る・予約して閉じる）。閉じた文書の行は一覧から消える（LIFE-01）。取り消しは無い。回帰 `doc-close-saved`・`doc-close-modified`
  - 訂正:

- **DOC-22** **Save**＝本体の保存（灰色は本体の判定）。窓なしの文書は本体のファイル ▸ 保存では保存できない（前面の文書にしか効かない）。回帰 `doc-save-modified`
  - 訂正:

- **DOC-23** **Revert**＝本体の復帰（本体の問いが先に出る）。回帰 `doc-revert-modified`
  - 訂正:

- **DOC-24** **Run Script...**＝`kfcResult` はその文書（`Document`）。**Label**＝文書のスクリプトラベル（`doc.insertLabel` と同じ）。文字の一致の操作（Clear Overrides・Create Outlines・Tate-chu-yoko）と Restore from Copy は灰色・行の Replace と Return は灰色（扉の文 `Replace: Find: Document lists the open documents and changes nothing - a row is not replaced.`）。回帰 `doc-runscript`・`doc-label-add`
  - 訂正:

## 4. ブックの行の Open Without a Window

- **DOC-30** ★**ブックを検索した結果のブックの行の右クリックに `Open Without a Window`**（チェックの付くトグル・既定 OFF・Save Panel Settings で保存）。ON のとき、そのブックの結果の行のクリック（矢印の着地も）で**章に窓を付けない**＝閉じた章は窓なしで開くだけ・何も選ばない・メッセージ欄 `<章> is open without a window (Open Without a Window) - nothing is selected.`。窓のある章は今までどおり。開いた章は利用者のもの（KFC は閉じない）。回帰 `wl-book-toggle`・`wl-book-nowindow-click`・`wl-book-click-off`
  - 訂正:

- **DOC-31** **Edit in Story Editor はブックの閉じた章の行でも**、章を窓なしで開いてからストーリーエディターを開く（トグルによらない＝ストーリーだけを開くのが目的・ST-31）。回帰 `wl-book-se-closed`
  - 訂正:

---

## ⬜ この章で未確認

- 窓なしの・まだ保存していない文書の Save（本体の「別名で保存」の窓）を実機で出していない（`doc-close-modified` は保存済みで変更のある文書の「保存しますか」だけ）。
- Revert の後の文書の行（文書の番地が変わる）を Run Script・Label で使ったときは回していない。
- Book Scope ON の Find: Document（ブックの開いている章だけ）は回していない。
- 開いている文書が多い（数十）ときの行の並びと速さ。
