# 26. 行にスクリプトを実行する（Special Operations ▸ Run Script...・変数 `kfcResult`）

> コード地図での位置: `ui/KFCRowScript.*`（行の物の参照・窓・実行）・`ui/KFCActionComponent.cpp`（押す・灰色）・`ui/KFCResultListWidgetMgr.cpp` の `RunScriptRows`。
> 設計書 `docs/superpowers/specs/2026-11-06-kfc-run-script-on-rows-design.md`（RS1〜RS10）。回帰の表＝`work/kbs-regress/rs-cases.tsv`（作る台本＝`work/note-scripts/2026-11-06-kfc-row-script/rs_cases.py`）。
> 作者の依頼（2026-11-06）「右クリックのメニューに、その行にスクリプトを実行できる仕組みをつくれないかな？　result　という変数が存在するとして、それに対して色々出来るような」「kfcResultかな」「これも貴方のおすすめで作ってみて欲しい」＝10-17 の見送り（E13）を作者が覆した。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. メニューと窓

- **RUN-01** ★**行の右クリックの Special Operations ▸ Run Script...**＝文字の行・物の行の両方（ABC 順＝Remove Label の後・Tate-chu-yoko／Show の前）。どの一覧の行でも出る。押した行、またはそれを含めて選んでいる行の全部に効く（ほかの特殊操作と同じ）。走っている間・着地の間は灰色。回帰 `rs-menu`・`tcy-menu-order`
  - 訂正:

- **RUN-02** ★押すと**スクリプトを書く窓**（ScriptUI＝スクリプトの窓）＝説明の1行（`kfcResult = each selected row's object - N row(s). One Ctrl+Z undoes the whole run.`）・複数行の欄（前に走らせたスクリプト・初めては使い方の見本のコメント）・`Load...`（.jsx を読む）・`Save As...`（.jsx に書く）・`Cancel`・`Run`。前に走らせたスクリプトは InDesign の利用者のフォルダの `KFCRunScript.jsx` に残る（次の起動でも出る）。Cancel＝`Run Script: cancelled - nothing was run.`。回帰 `rs-window-cancel`
  - 訂正: 2026-11-06 朝 作者「いま//で説明されていますが、Helpボタンをいれて説明はそちらでしてほしい」＝欄は初めは空・説明は Help... の窓へ（RUN-03）。
  - 訂正: 2026-11-06 夕 作者「ＲｕｎＳｃｒｉｐｔのダイアログの上の部分にkfcResult= とありますが、そこに今はいっているのを表示して欲しい」「alert(kfcResult)で表示されるないようでいいかな」＝窓の1行目は `kfcResult = [object Text] - 3 row(s). One Ctrl+Z undoes the whole run.`（各行の物を取り出して `String()` の字・種類が混ざれば `[object Text] (2), [object Rectangle] (1)`・出た順）。それまでは `kfcResult = each selected row's object - N row(s).`。回帰 `rs-window-line-one`・`rs-window-line-mixed`

- **RUN-03** ★**窓の左下に `Help...`**（右に Load... / Save As... / Cancel / Run）＝押すと説明の窓（`Run Script on Rows - Help`・読むだけの複数行の欄・選んでコピーできる・OK で閉じる）。**日本語の InDesign では日本語**（How to Use と同じ＝UI の言語）。中身＝kfcResult が行ごとに何か・kfcIndex・kfcCount・Ctrl+Z 1回・走らせない行・ボタン・例4つ・**UXP は台本の中から `app.doScript(…, ScriptLanguage.UXPSCRIPT, …)` で**（値は `script.setResult`・引数は `script.args`・await より後の変更は取り消しの1段の外）。★**欄は初めは空**・前に保存された台本が 11-06 までの見本（// の行）と同じなら空として開く（改行の種類と末尾の空白は見ない）・自分の台本は残る。（設計書 `2026-11-06-kfc-label-spelling-runscript-design.md` RH1〜RH3・作者「UXPつかいたければDoScritでUXPをつかえばいい」＝Run Script 自体は ExtendScript だけ）回帰 `rs-help-window`・`rs-box-old-sample`・`rs-box-kept`・`rs-box-none`
  - 訂正:

## 2. kfcResult

- **RUN-10** ★**`kfcResult`＝その行が指す物**＝一致の行＝一致の文字（`Text`・幅0の一致は `InsertionPoint`・表のセルの中の一致はその文字＝`parent` が `Cell`）／物の行＝そのページアイテム／Story の行＝そのストーリー（内部は `XmlStory`）／Table の行＝その表／表の行の一覧の行＝その行（`Row`）／スクリプトラベルのセルの行＝そのセル。ほかに `kfcIndex`（0から）と `kfcCount`。回帰 `rs-text`・`rs-cell-parent`・`rs-zero-width`・`rs-items`・`rs-story`・`rs-table`
  - 訂正: 2026-11-06 夕＝Find: Document の行＝その文書（`Document`・DOC-24）。作者の問い「つまりkfcResultの内容は、検索結果、ですよね。」（そう＝その行の検索結果）・「QR コードや圏点、割注、ルビの場合どうなります？」＝QR Code の行は QR コードの入った枠・圏点／割注／ルビ／Spelling の行はその文字（`Text`）を試験で確かめる（`rs-qr`・`rs-kenten`・`rs-warichu`・`rs-ruby`・`rs-spelling`）。ルビの読みは `kfcResult.rubyString = "こはくねこ"` で書ける（`rs-ruby-reading`・Ctrl+Z 1回で戻る）。

- **RUN-11** 行の場所は**今の場所**（文字の行はテキストフォーカスが追った場所＝クリックの着地と同じ）。見つからない行・物が消えた行は走らせずに数える。★**文書が開いていない行（ブックの閉じた章）も走らせずに数える**（開けば走る）。開いているかは**章のファイルで聞く**（結果が持つ番地では聞かない＝後で開いた別の文書がその番地をもらうと、その文書の物に走っていた）。回帰 `rs-book-closed-reused`・`rs-book-open`
  - 訂正: 2026-11-06（2.1.0 からの変更の再検査・ブロック20 の D20-a）＝それまでは番地の比較で聞いていた。閉じた章の docRef が別の開いている文書を指す状態（番地の使い回しが残す状態）で、その文書の本文に走った（`dog one`）。

## 3. 走らせ方

- **RUN-20** ★行ごとに1回ずつ・**全部で取り消し1段**（Edit メニューの名前 `KFC Run Script`）。回帰 `rs-write`・`rs-write-undo`
  - 訂正:

- **RUN-21** 1つの行のエラーで止めない＝数えて最初のエラーを言う（`Run Script: ran on 2 of 3 row(s); 1 error - P1 cat: Error: boom`）・エラーの行のそれまでの変更は残る。JavaScript になっていないスクリプトは何も走らせずに言う（`Run Script: the script did not compile - SyntaxError: ...`）。回帰 `rs-error`・`rs-syntax`
  - 訂正:

- **RUN-22** 行に印は付けない（スクリプトが何を変えたかは KFC には分からない）。行は描き直すだけ＝字を書き換えた行は次のクリックで Missing になりうる。
  - 訂正:

---

## ⬜ この章で未確認

- スクリプトが文書を閉じる・行の物を消すときの行の描き直し（落ちないことは試していない）。
- `Load...`・`Save As...` のファイルの窓（実機の人の手でだけ）。
- 長く走るスクリプトの止め方（ExtendScript は `IScriptRunner::CancelScriptExecution` で止まる＝作っていない）。
