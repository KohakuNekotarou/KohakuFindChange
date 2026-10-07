# 12. フライアウトと設定の保存

> コード地図での位置: ブロック **5**（`ui/KFCActionComponent.cpp`）・**3**（`ui/KFCUI.fr` の MenuDef／ActionDef）・**4**（`ui/KFCPanelState.*`）。
> 各項目の働きはそれぞれの章（検索＝第2章、置換＝第6章、窓の工夫＝第13章、ブックパネル＝第14章、アプリケーションバー＝第15章）。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. フライアウトの並び

- **SET-01** パネルのフライアウトは、上からこう並ぶ（`──` は区切り線）。

  ```
  Open Find/Change...
  Find in Document
  Change All in Book (No List)
  Clear Results
  Run Saved Queries...
  ──────────
  Book Scope
  Find/Change Selected Documents (Book)
  Hide Previous Chapter
  Link the Application Bar's Search Field to This Panel
  Translucent Find/Change
  Minimizable Find/Change
  Translucent Panel
  Remember Book Panel Placement
  ──────────
  Save Panel Settings
  ──────────
  How to Use...
  About This Plug-in...
  ```

  2行目の名前は範囲で変わる（`Find in Book`／`Find in Document`／`Find in Story` など＝SET-03。上の絵は Book Scope OFF・検索: ドキュメントのとき）。
  `Book Scope` から `Remember Book Panel Placement` までの8つはトグルで、ON のとき左にチェックの印が付く（最初は `Hide Previous Chapter` だけ ON＝SET-23）。
  （`Change Checked` と `Show Changes by KohakuFindChange` は 2026-10-06 に外した。`Run Saved Queries...` と `Find/Change Selected Documents (Book)` は 2026-10-07 に足した＝第16章・SCOPE-26〜。`Run Saved Queries...` は同じ日の夜に `Clear Results` の下へ1つ下げた＝作者の依頼。）
  - 訂正:

- **SET-02** ★`Find in …`・`Change All in Book (No List)`・`Clear Results`・`Run Saved Queries...` の間に区切り線は無い（探すことと、一覧を使わない置換と、その前の片付けが1つのまとまり）。
  トグルの並びは、検索にかかわるもの（Book Scope・Find/Change Selected Documents (Book)・Hide Previous Chapter・アプリケーションバー）が先、窓の見た目が後。
  - 訂正:

- **SET-03** 項目の名前は**どの UI 言語でも英語**（GEN-35）。名前が変わるのは2つ ―― `Find in …`（範囲によって `Find in Book`／`Find in Document`／`Find in Story` など＝GEN-28）と、
  Find/Change Selected Documents (Book) でブックの一部を選んでいる間の `Find in Selected Documents`／`Change All in Selected Documents (No List)`（SCOPE-31・2026-10-07）。
  灰色のときも、範囲つきの名前は出る。
  - 訂正:

---

## 2. 灰色になるとき

- **SET-04** 検索・置換・Change All が**走っている間は、灰色にできる項目はすべて灰色**（GEN-33）。
  `Open Find/Change...`・`Save Panel Settings`・`How to Use...`・`About This Plug-in...` の4つはこの確かめを受けないが、
  走っている間は進捗バーがモーダルなので、フライアウトそのものが開けない（2026-08-03 実測）。
  - 訂正:

- **SET-05** `Find in …` ＝範囲に対象が無い（Book Scope ON でブックが無い・章の無いブック／OFF で文書が無い）か、検索と置換のタブが Object／Colour のとき灰色（FIND-17）。
  `Change All in Book (No List)` ＝Book Scope が OFF・対象のブックが無い・パネルにヒットの行がある・タブが Object／Colour・検索する文字も形式も無いとき灰色（REP-33）。
  `Clear Results` ＝ヒットの行が無いとき灰色（0件のブックの行だけのときも・REP-34）。
  置換の文字は見ない（空でも「一致を消す」としてやれる）。
  `Run Saved Queries...` ＝走っている間（SET-04）のほかは灰色にならない（文書が無くても・結果が出ていても押せる＝QRY-02）。
  - 訂正:

- **SET-05b** `Find/Change Selected Documents (Book)` ＝Book Scope が ON のときだけ押せる。灰色でもチェックの印は見えたまま（SET-06 と同じ扱い・SCOPE-28）。
  押すと ON／OFF が入れ替わるだけで、メッセージ欄には何も書かない（どの文書を相手にするかは、検索・置換・Run が走る瞬間に決まる＝SCOPE-30）。
  - 訂正:

- **SET-06** `Hide Previous Chapter` ＝Book Scope が ON か、ブックの結果が出ているときだけ押せる。灰色でもチェックの印は見えたまま（JMP-27）。
  - 訂正:

- **SET-07** `Book Scope` と、窓・ブックパネル・アプリケーションバーのトグル（`Translucent …`・`Minimizable Find/Change`・`Remember Book Panel Placement`・`Link the Application Bar's …`）は、
  **いつも押せる** ―― 当てる窓がまだ無くても（パネルがドックの中・ダイアログが閉じている・ブックが無い）、設定を立てておけば、窓が来たときに効く
  （★「ドックの中では効かないが押せる」は KCM（当時の KESCM）の 2026-07-29 の決定を引き継いだ）。チェックの印で ON／OFF が分かる。
  - 訂正:

- **SET-08** `Open Find/Change...`・`Save Panel Settings`・`How to Use...`・`About This Plug-in...` は、いつも押せる（Open Find/Change は文書が0でも開く＝GEN-04）。
  - 訂正:

- **SET-09** 窓とアプリケーションバーのトグルを押すと、メッセージ欄にどうなったかを書く ―― `Translucent panel: on.`／`Translucent panel: off.`。
  ON にしても当てる先がまだ無いときは ――
  `Translucent panel: on - has no effect while the panel is docked.`／
  `Translucent Find/Change: on - applies when the Find/Change dialog is open.`（Minimizable Find/Change も同じ文）／
  `Application Bar link: on - works while the Application Bar's search field is shown.`
  - 訂正:

---

## 3. 右クリックメニュー

- **SET-10** 右クリックのメニューがあるのは**ヒットの行だけ**で、項目は `Replace` の1つ（ROW-32）。ブック・文書・ストーリーの行には無い。
  （Reject／Accept の仲間・Replace Again・Check All／Uncheck All・本／文書／ストーリー／run の行のメニューは 2026-10-06 に外した。）
  - 訂正:

- **SET-11** `Replace` が灰色のとき（置換済み・ロック・検索の一致でない）は、メニューが開かない（本体の振る舞い＝2026-08-01 実測・作者がこのほうがよいと受け入れた）。
  理由は出さない（右クリックの理由の層は 2026-10-06 に外した＝ROW-29）。
  - 訂正:

- **SET-12** （2026-10-06 に外した＝Check All／Uncheck All。チェックボックスごと＝F16）
  - 訂正:

---

## 4. キーボードショートカット

- **SET-13** ★ショートカットを割り当てられるのは**5つだけ** ―― `Open Find/Change...`・`Find in …`・`Change All in Book (No List)`・`Run Saved Queries...`・`Clear Results`
  （2026-09-27 に検索、10-01 に Open Find/Change＝作者の決定。10-06 の設計（第5版）で Change All in Book と Clear Results を足し、Change Checked と Show Changes を外した。
  10-07 に Run Saved Queries... を足した（作者の依頼）。`Find/Change Selected Documents (Book)` は Book Scope と同じく割り当てない。
  行の置換はショートカットでなく Return キー＝F17）。
  キーボードショートカットの設定の「パネルメニュー」に `Kohaku Find/Change: <名前>` として出る（KCM と同じ形）。ほかの項目（行の `Replace` も）は出ない。
  ⚠`Find in …` は、設定の画面ではリソースの名前の `Search` で出る見込み（メニューで名前を範囲に変えるのは、表示する直前だけなので）。
  - 訂正:

- **SET-14** ショートカットで押しても、メニューと**同じ灰色の確かめ**が働く（押せない状態ならショートカットも効かない）。
  - 訂正:

---

## 5. 設定の保存

- **SET-15** ★設定は**自動では保存しない** ―― `Save Panel Settings` を押したときだけ保存し、**次の起動で自動で読み戻す**（保存は手動・復元は自動＝KCM（当時の KESCM）と同じ）。
  「利用者が残すと言っていない設定は、後で利用者が説明できない」ため（例外は SET-20）。
  - 訂正:

- **SET-16** 保存するもの ―― Translucent Panel／Translucent Find/Change／Minimizable Find/Change／Link the Application Bar's Search Field／
  Hide Previous Chapter／Find/Change Selected Documents (Book)（2026-10-07・キー `selectedDocuments`＝SCOPE-34）／Remember Book Panel Placement と、ブックパネルの位置（開いていれば今の位置、無ければ最後に覚えた位置）。
  （保存したクエリの並びは、このファイルに入れない ―― InDesign を起動している間だけ覚え、ファイルにするのは `Save Order...` で利用者が選んだ場所＝QRY-14〜16・QRY-26。2026-10-07 の昼の版が書いていた `KFCQueryOrder.txt` はもう使わない。）
  - 訂正:

- **SET-17** ★**保存しないもの＝Book Scope**（GEN-26・「次に何を探すか」は設定ではなく今の作業）。起動のたびに OFF から。
  - 訂正:

- **SET-18** 置き場所は InDesign の環境設定のフォルダー（ロケールの入ったほう＝`%APPDATA%\Adobe\InDesign\Version XX.0\<言語>\`）の **`KBSPanelState.json`**。
  サブフォルダーは作らない（そのフォルダーは InDesign が自分の設定のために作ってある）。
  - 訂正:

- **SET-19** ★保存すると、メッセージ欄に**ファイルのフルパスだけ**を出す（2026-08-08 の決定＝「どこに保存したか」の答えで、探す・控える・消すのに使える）。
  失敗したら、どこで失敗したかの語つきで `Save failed (folder).`／`Save failed (open).`／`Save failed (write).`／`Save failed (replace).`。
  - 訂正:

- **SET-20** ★**Remember Book Panel Placement だけは、押した瞬間に保存する**（2026-09-25 の決定）。
  書くのは**この項目の ON／OFF だけ**で、ほかの項目はファイルの中身のまま触らない（利用者がまだ保存していない途中の設定を、勝手に保存しないため）。
  メッセージ欄には `Remember book panel placement: on`（または `off`）と、次の行にファイルのパス。書けなければ ` - but it could not be saved (…).`
  ON の間は、ブックパネルの位置も、ブックパネルが閉じるときと InDesign の終了のときに同じように書く（うまくいったときは黙っている・→ 第14章）。
  - 訂正:

- **SET-21** ★SET-20 の書き込みのとき、ファイルが壊れていたら（落ちて途中で切れたなど）、**読める項目だけ残して書き直す**
  （2026-09-28 の決定＝それまでは書き込みを断っていて、ブックパネルの位置が残らなかった）。
  そのときは ` (the settings file was damaged and has been rewritten with what could be read)` と言う。
  （Save Panel Settings は全部の項目を書くので、壊れていても丸ごと書き直す。）
  - 訂正:

- **SET-22** 書き込みはいったん別のファイル（`KBSPanelState.json.tmp`）に書き、読み返して同じだと確かめてから置き換える（途中で落ちても前のファイルが残る＝2026-09-28）。
  - 訂正:

- **SET-23** 読み戻しは1回の起動で1回。ファイルが無ければ**既定のまま** ―― Hide Previous Chapter は **ON**、ほかのトグルはすべて **OFF**。
  ファイルが途中までしか読めなかったら、何も当てない（全部か無し）。読み戻しでは窓には何もしない（窓が来たときに効く＝SET-07）。
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- SET-13：`Find in …` が設定の画面で `Search` と出るのは、リソースの名前から読んだもので、実機の設定の画面では見ていない。
- SET-13：1.0.0 から更新するとショートカットが外れる（README）。1.2.0 の中で版を上げても外れないかは確かめていない（ActionID は変えない約束）。
- SET-04：走っている間に、`Open Find/Change...` のショートカット（灰色の確かめを受けない）を押したらどうなるかは測っていない（進捗バーがモーダルなので届かない見込み）。
- SET-18：`<言語>` のフォルダー名（例 `ja_JP`）と `Version XX.0` の数は、この章のためには実機のパスを見ていない。
