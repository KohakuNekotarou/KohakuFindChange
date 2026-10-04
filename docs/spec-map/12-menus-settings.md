# 12. フライアウトと設定の保存

> コード地図での位置: ブロック **5**（`ui/KBSActionComponent.cpp`）・**3**（`ui/KFCUI.fr` の MenuDef／ActionDef）・**4**（`ui/KBSPanelState.*`）。
> 各項目の働きはそれぞれの章（検索＝第2章、置換＝第6章、窓の工夫＝第13章、ブックパネル＝第14章、アプリケーションバー＝第15章）。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. フライアウトの並び

- **SET-01** パネルのフライアウトは上から ――
  `Open Find/Change...`／`Find in …`／`Change Checked`／（区切り）／`Show Changes by KohakuFindChange`／（区切り）／
  `Book Scope`／`Hide Previous Chapter`／`Link the Application Bar's Search Field to This Panel`／`Translucent Find/Change`／
  `Minimizable Find/Change`／`Translucent Panel`／`Remember Book Panel Placement`／（区切り）／
  `Save Panel Settings`／（区切り）／`How to Use...`／`About This Plug-in...`。
  - 訂正:

- **SET-02** ★`Find in …` と `Change Checked` の間に区切り線は無い（2026-09-27 の決定）。`Show Changes` は `Change Checked` の下で、区切り線の後（2026-09-29・作者が決めた場所）。
  トグルの並びは、検索にかかわるもの（Book Scope・Hide Previous Chapter・アプリケーションバー）が先、窓の見た目が後。
  - 訂正:

- **SET-03** 項目の名前は**どの UI 言語でも英語**（GEN-35）。名前が変わるのは `Find in …` だけ（範囲によって `Find in Book`／`Find in Document`／`Find in Story` など＝GEN-28）。
  灰色のときも、範囲つきの名前は出る。
  - 訂正:

---

## 2. 灰色になるとき

- **SET-04** 検索・置換・Show Changes が**走っている間は、灰色にできる項目はすべて灰色**（GEN-33）。
  `Open Find/Change...`・`Save Panel Settings`・`How to Use...`・`About This Plug-in...` の4つはこの確かめを受けないが、
  走っている間は進捗バーがモーダルなので、フライアウトそのものが開けない（2026-08-03 実測）。
  - 訂正:

- **SET-05** `Find in …` ＝範囲に対象が無い（Book Scope ON でブックが無い・章の無いブック／OFF で文書が無い）か、検索と置換のタブが Object／Colour のとき灰色（FIND-17）。
  `Show Changes` ＝範囲に対象が無いとき灰色。`Change Checked` ＝チェックした行が無いか、一覧にチェックボックスが1つも無いとき灰色（REP-08）。
  検索と置換に書いた文字は見ない（置換の文字が空でも「一致を消す」としてやれる）。
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

- **SET-10** 行の右クリックメニュー（上から）――
  **ブックの行・文書の行**＝`Replace`／`Reject All Changes by KohakuFindChange in This Document`／`Replace Again (Current Find/Change Settings)`／
  `Check All`／`Uncheck All`／`Accept All Changes by KohakuFindChange in This Document`（ブックの行では、文書1つのための項目は灰色。ブック全体の置換は Change Checked）。
  **ストーリーの行**＝`Replace`／`Reject Changes by KohakuFindChange in This Story`／`Accept Changes by KohakuFindChange in This Story`／`Replace Again (Current Find/Change Settings)`／`Check All`／`Uncheck All`。
  **ヒットの行**＝`Replace`／`Reject Change by KohakuFindChange`／`Accept Change by KohakuFindChange`。
  **run の行**＝`Reject Changes by KohakuFindChange in This Run`／`Accept Changes by KohakuFindChange in This Run`。
  どの項目も、右クリックした行で、やることがあるときだけ押せる。
  - 訂正:

- **SET-11** 右クリックメニューの項目が**すべて灰色**だと、メニューは開かない（本体の振る舞い＝2026-08-01 実測・作者がこのほうがよいと受け入れた）。そのときは、理由がメッセージ欄に出る（ROW-29）。
  - 訂正:

- **SET-12** Check All／Uncheck All は**フライアウトには無く、右クリックにだけ**ある（2026-08-01 から＝どこまで付けるかを、右クリックした行で決めるため・ROW-23）。
  - 訂正:

---

## 4. キーボードショートカット

- **SET-13** ★ショートカットを割り当てられるのは**4つだけ** ―― `Open Find/Change...`・`Find in …`・`Change Checked`・`Show Changes by KohakuFindChange`
  （2026-09-27 に検索と Change Checked、09-29 に Show Changes、10-01 に Open Find/Change＝どれも作者の決定）。
  キーボードショートカットの設定の「パネルメニュー」に `Kohaku Find/Change: <名前>` として出る（KCM と同じ形）。ほかの項目は出ない。
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
  Hide Previous Chapter／Remember Book Panel Placement と、ブックパネルの位置（開いていれば今の位置、無ければ最後に覚えた位置）。
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
- SET-10：右クリックメニューの項目の並びは、リソースの位置の番号から読んだもので、この章のためには実機の表示を見ていない。
