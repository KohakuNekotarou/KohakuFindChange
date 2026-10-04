# 08. Show Changes by KohakuFindChange

> コード地図での位置: ブロック **9b**（`model/KFCShowChanges.*`）。行の作り方は検索の `HitBuilder`（ブロック8）を借り、記録の読み方は `KFCTrackChange`。
> 範囲は第3章 SCOPE-25、記録の形と却下・承認は第7章、行の見た目は第4章。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 何をするか

- **SHOW-01** ★**文書に残った `KohakuFindChange` の変更履歴の記録から、一覧を作り直す**（2026-09-29 の作者の設計）。
  新しい検索をした後・文書を閉じて開き直した後・InDesign を再起動した後でも、KFC の置換をもう一度一覧にできる。
  フライアウトの **Show Changes by KohakuFindChange**（Change Checked の下・区切り線の後）。
  - 訂正:

- **SHOW-02** ★**記録だけがすべて** ―― 検索のことは何も保存しない（スクリプトのラベルにも、設定ファイルにも、保存した検索にも書かない＝2026-09-29 の決定B）。
  記録が「どの置換（run）が書いたか」「何行目か」を時刻で持っているので、それだけで一覧が組める（TRK-03）。
  - 訂正:

- **SHOW-03** ★作り直した一覧では**置換はできない** ―― 却下（Reject）と承認（Accept）だけ（2026-09-29 の決定B）。
  もう一度置換したいときは、検索し直す。チェックボックスは無く（ROW-21）、Change Checked は灰色。
  - 訂正:

---

## 2. 範囲と、断るとき

- **SHOW-04** 範囲は **Book Scope** だけで決まる ―― ON＝ブックの全章（検索と同じブック・同じ開き方＝章を1つずつ窓なしで開き、読んだらすぐ閉じて返す）、
  OFF＝**アクティブな文書だけ**。⚠「検索:」には従わない（All Documents や Story でも、アクティブな文書1つ）（SCOPE-25）。
  - 訂正:

- **SHOW-05** 断る文 ―― ほかの実行中＝`Show Changes by KohakuFindChange is already running.`／`Another Kohaku Find/Change run is already in progress.`、
  ブックが無い＝`Book Scope is on, but no book is open.`、章の無いブック＝`That book has no chapters.`、文書が無い＝`No open document.`。
  断ったときは**前の一覧を残す**。メニューは、範囲に対象が無いときと実行中は灰色。
  - 訂正:

- **SHOW-06** 断る理由を通り過ぎた所で、**前の一覧は消える**（検索と同じ commit point・FIND-18）。
  - 訂正:

---

## 3. 読み方

- **SHOW-07** 文書の中の**すべてのテキスト**（利用者から見えない内部のストーリーも含む）の記録を読む ―― 記録はどこにあっても数える
  （Accept All が承認するものと同じ範囲。検索より広い）。
  - 訂正:

- **SHOW-08** 1行＝**1回の置換**（記録の時刻1つ）。行の位置は、その置換が書いた文字（挿入）の今の位置。空で置換した行は、削除の位置に幅0で出る。
  - 訂正:

- **SHOW-09** 読むだけで、文書を「変更あり」にしない（読んだ章も、未変更のまま閉じて返す）。
  - 訂正:

- **SHOW-10** 進捗バーが出る ―― 題はブックなら `Reading the book's changes...`、文書なら `Reading changes...`。Cancel で**全部捨てる**（`Show Changes cancelled.`）。
  - 訂正:

- **SHOW-11** 集める上限は検索と同じ **5,000 行**（GEN-34）。止まったら ` Stopped at the 5000 safety limit.`
  - 訂正:

---

## 4. 一覧の形

- **SHOW-12** 木は **文書の行 → run の行 → ストーリーの行 → ヒットの行**（ブックならその上にブックの行）。run の行とストーリーの行は開いて出る（ROW-03）。
  - 訂正:

- **SHOW-13** **run の行**＝1回の置換（Change Checked の1回、または行のメニューの Replace／Replace Again の1回）。
  文字は `<日付> <時刻>  (行の数)` ―― その置換を始めたときの**この PC の時刻**で、日付は OS の短い形、時刻は**秒まで**
  （同じ1分の中の2回の置換が同じ行に見えたのを、秒を足して直した＝2026-09-29）。
  - 訂正:

- **SHOW-14** run の行は**新しい置換が上**。run の中のヒットの行はページ順（検索と同じ並べ方・番号の付け方）。
  2回の置換が同じストーリーを書いていれば、そのストーリーの行は run ごとに別々に出る。
  - 訂正:

- **SHOW-15** ヒットの行は、検索の行と**同じ作り方**（ページの位置・状態の語・50文字の行）。違いは、すべてが「置換済み」の行であること。
  前後に隣り合って書いた置換は、削除の記録が最後の行にまとまっているので、それより前の行は「元の文字」を持たない（TRK-07）。
  - 訂正:

- **SHOW-16** 非表示の条件の中の行は ` hidden condition` と書き、条件を表示すると文字が戻る場所のページを出す（ROW-13・JMP-11）。
  - 訂正:

---

## 5. メッセージ欄

- **SHOW-17** 見つかったとき ―― `Found N change(s) by KohakuFindChange in R run(s) - right-click a row to reject or accept them. To replace again, search again.`
  （N＝行の数、R＝run の数。ブックなら全部の章を通して数える）。
  - 訂正:

- **SHOW-18** 見つからなかったとき ―― `No changes by KohakuFindChange in this book.`／`No changes by KohakuFindChange in this document.`
  - 訂正:

- **SHOW-19** 読めなかった章（`could not be read`）・開けなかった章・閉じられなかった章は、検索と同じ形で名前つきで書き足す（FIND-36・SCOPE-15）。
  - 訂正:

---

## 6. 作り直した一覧でできること

- **SHOW-20** ヒット・ストーリー・文書の行の Reject／Accept と、文書の Accept All は、第7章のとおり。
  **run の行**の右クリックは `Reject Changes by KohakuFindChange in This Run`／`Accept Changes by KohakuFindChange in This Run` ＝その run の、
  その文書の中の行で、記録が残っているものすべて（`Rejected N row(s) of this run - back to their original text. To replace again, search again.`）。
  - 訂正:

- **SHOW-21** 却下した行は ` rejected` と書く（チェックボックスが戻らないので、語で言う＝ROW-15）。承認した行は ` accepted`。
  - 訂正:

- **SHOW-22** 行をクリックするとジャンプする（置換した行として、記録の今の位置へ＝JMP-07 ②）。
  ⚠行が見つからなくても**探し直しはしない**（探し直すのは置換していない行だけで、作り直した一覧の行はすべて置換済み。検索の条件も無い）。
  記録で見つからないとき（変更履歴パネルで承認・却下した・取り消した）は `The replacement is no longer here - undone, or edited since.` と言い、行に `missing` は付けない。
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- SHOW-13：日付の「OS の短い形」は Windows の地域設定で変わる（日本語なら `2026/10/04` の形のはず）。実機の表示は、この章のためには見ていない。
- SHOW-11：Show Changes の上限 5,000 は実機で踏んでいない（GEN-34 の未確認と同じ）。
- SHOW-07：利用者から見えない内部のストーリーに KFC の記録が付く場面が実際にあるかは知らない（コードは「どこにあっても数える」と決めているだけ）。
- SHOW-22：コードから読んだ振る舞いで、作り直した一覧の行が見つからない場面を実機では試していない。
