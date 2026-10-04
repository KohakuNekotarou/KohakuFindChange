# 06. 置換 ― Change Checked と行のメニューの Replace

> コード地図での位置: ブロック **9**（`model/KFCReplaceEngine.*`）。
> 変更履歴の署名・色・却下と承認は第7章、取り消し・やり直しへの追従は第9章。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 何で、どう置換するか

- **REP-01** 置換するのは**チェックを付けた行だけ**。書く文字は本体の「検索と置換」の置換側
  （置換文字列／字形タブなら置換字形／文字種変換タブなら置換後の文字種）。
  置換文字列が**空なら一致を消す**（空でも灰色にしない＝空は「消して」という正しい頼み）。
  - 訂正:

- **REP-02** GREP の後方参照（`$1` など）やエスケープは **KFC が読まず、本体の置換がそのまま解釈する**（KFC は検索と置換の設定を読むだけで、書き換えない）。
  - 訂正:

- **REP-03** ★置換は**1件ずつ** ―― ストーリーごとに、本体の「次を検索」のコマンドで一致へ進み、それがチェックした行なら、本体の置換のコマンドでその一致を置換する。
  ★この骨格は確定（2026-07-31・再設計を提案しない）。本体の「すべてを置換」は使えない
  ＝「チェックした行だけ」を渡す口が無く、行ごとに置換後の範囲も返さないため。
  - 訂正:

- **REP-04** 行は「どのスレッドの、どこから、何文字」で見分ける。置換の途中で、置換そのものが新しく作った一致（検索では出なかったもの）は**飛ばす**。
  そのせいで行の一致に出会えなかった行は書かず、`missing` と言う。
  - 訂正:

- **REP-05** 歩く向きは前向き。★ただし GREP の検索文字列に `^`（段落の頭）があるときだけ、**書くときの歩きは後ろ向き**（2026-09-27 の決定）。
  検索と、書く前の照合は前向きのまま。
  - 訂正:

- **REP-06** ★置換する一致に**重なる・接する「まだ承認していない変更」**は、誰のものでも**先に承認してから**置換する（2026-09-27 の決定＝その部分だけ）。
  承認した数はメッセージ欄に ` N pending tracked change(s) accepted first.` と書く（変更履歴パネルから消えるので）。
  理由＝作者がまだ承認していない挿入を置換すると、記録が残らず、却下で戻せなくなるため。
  後注の終わりで置換しない一致の周りは承認しない（2026-10-02）。
  - 訂正:

---

## 2. Change Checked の前の確かめ

- **REP-07** 断る理由は次の順に調べる ――
  ①ほかの実行中（`A replace is already running.`／`Another Kohaku Find/Change run is already in progress.`）
  ②結果が無い（`No results to replace - run a search first.`）
  ③記録から作り直した一覧（`Change Checked: these rows were rebuilt from Track Changes - search again to replace.`）
  ④置換の報告（`This is the last replace's report - search again to replace more.`）
  ⑤チェックが0（`Nothing checked.`）
  ⑥検索の条件が変わった（§3）
  ⑦置換側の設定が読めない（`Find/Change settings are unavailable - nothing was changed.`）
  ⑧文書が変わった（§4）。
  - 訂正:

- **REP-08** メニューの Change Checked が灰色になるのは、**チェックした行が無い**か、**一覧にチェックボックスが1つも無い**とき（③④⑤）。
  ★**確認のダイアログは出さない**（2026-09-27 の決定）＝全部が1回の取り消しで戻り、保存もしないため。
  - 訂正:

---

## 3. 検索の条件が変わっていたら

- **REP-09** 検索と置換の**タブが検索したときと違う** → `The Find/Change dialog is on a different tab than when this search ran. Search again.`
  **結果は残す**（タブは1クリックで戻せるので）。
  - 訂正:

- **REP-10** ★**検索文字列・スイッチ**（大文字小文字・単語単位・かな・全角半角・範囲の5項目）・**検索形式**が変わった →
  **結果を消して**断る：`The Find/Change query has changed since this search - the results have been cleared. Search again.`
  （2026-08-03 の決定＝ほかの断りはすべて結果を残すが、これだけは例外。残しても、もう使えない一覧なので）
  - 訂正:

- **REP-11** 比べないもの ―― 検索の方向（KFC は前向きだけ＝GEN-22）と、**置換側**の設定（置換文字列など＝何が見つかるかではなく、何を書くかなので）。
  - 訂正:

- **REP-12** タブを確定し直せない → `The Find/Change tab could not be set - nothing was changed. Try reopening Edit > Find/Change.`（結果は残す）。
  - 訂正:

---

## 4. 文書が変わっていたら ―― 書く前の照合

- **REP-13** ★書く前に、チェックした行のある章を**全部開き**（閉じていれば開き直す）、**1文字も書く前に**照合する ――
  ①そのストーリーの版（変更カウンタ）が、KFC が最後に知っている版のままか
  ②行の文字（一致と前後）が、検索したときのまま読めるか
  ③同じ条件で歩き直すと、行の位置に同じ長さの一致が立つか。
  （★2026-08-10 の決定＝「この章は触られていないように見えるか」ではなく、**一致そのものの位置**で確かめる）
  - 訂正:

- **REP-14** ★1行でも合わなければ、**何も書かずに止め**、警告アラート `検索結果に変化を確認しましたので、置換を中止しました。`
  （ブックの章なら `「章名」の検索結果に変化を…`・英語 UI は `The search results have changed, so the replace was stopped.`）を出し、**結果を消す**。
  メッセージ欄は `Replace cancelled - nothing was changed. Results cleared - the document changed. Search again.`
  ★「それでも続ける」の選択肢は無い（2026-08-10 の決定＝崩れた作業リストは、どう答えても安全に置換できない）。
  - 訂正:

- **REP-15** ★版を照合に使う理由 ―― 利用者の入力・ほかの操作の Ctrl+Z・変更履歴パネル・スクリプトで動いたストーリーには、**推測で書かない**
  （2026-09-29 の決定＝「安全第一」・検索し直してもらう）。KFC 自身の書き込みの取り消し・やり直しは追いかけているので、それで止まることはない（第9章）。
  - 訂正:

---

## 5. 書き込みと、その後

- **REP-16** Change Checked は**ブック全体でも1回の取り消しの段**（「編集 > 取り消し」の名前は日本語 UI で `置換`、英語 UI で `Replace`）。
  どの章を前にして Ctrl+Z しても、全部の章が一緒に戻る（章ごとの段にすると、取り消した章以外の履歴だけが消えて戻せなくなった＝2026-07-28 実測）。
  - 訂正:

- **REP-17** ★チェックした行のある章は**全部開いてから書く**（キャンセルでブック全体を戻せるように）。
  一度に開いていられないほど大きいブックは、**チェックする章を絞って分けて**置換する（2026-08-05 の決定）。
  - 訂正:

- **REP-18** ★進捗バーの**キャンセルは全部を元に戻す** ―― 本文も、一覧も、「変更あり」の印も、KFC が開いた章も。`Replace cancelled - nothing was changed.`
  本体がエラーを出して止まったときも全部戻し、`Replace stopped - InDesign reported an error, so nothing was changed ("…").` と本体の言葉を添える。
  - 訂正:

- **REP-19** チェックした行が置換されなかったときは、**必ず数えて理由を言う**（合計が黙って減らないように）――
  ロック＝` N hit(s) left alone - locked layer or story (those can be searched, not changed).`／
  見つからない＝` ! N hit(s) missing - not found when the chapter was searched again.`（`!` で目立たせる・2026-08-04 の決定）／
  後注の終わり＝` N hit(s) in endnotes not replaced - …`／本体が断った＝` N hit(s) could not be changed - InDesign refused the change there.`
  行にもそれぞれの語が付く（ROW-13）。
  - 訂正:

- **REP-20** ★**後注の終わりで終わる一致は置換しない**（本体の置換がそこで後注を壊すため＝2026-09-27 の決定）。
  - 訂正:

- **REP-21** ほかのチェックした行が脚注・表・アンカー付きオブジェクトを丸ごと消したとき、その中にあった行は**一緒に消え**、`deleted` が付く（行き先も無い）。
  - 訂正:

- **REP-22** 終わったときの文 ―― `N replaced.`（ブックは `N replaced in M chapter(s).`）。
  1つでも書いたら ` Not saved - check them and save yourself.` と、` Replaced with Track Changes on - Reject Change on a row's right-click menu takes it back.` が続く。
  - 訂正:

- **REP-23** 一覧は**置換の報告**に変わる（GEN-30 ②）―― チェックしなかった行は消え、関わった行と、ロックの行・すでに何か言うことのある行だけが残り、チェックボックスは消える。
  - 訂正:

- **REP-24** ★何も保存しない（GEN-17）。書いた章のうち **KFC が開いた章には窓を付け**、**何も書かなかった章は閉じて返す**。
  利用者が窓なしで持っている文書は隠したまま（` N document(s) without a window were changed - still hidden.`）。
  - 訂正:

---

## 6. 行のメニューの Replace

- **REP-25** **ヒットの行の Replace**＝その1行を、**チェックの有無にかかわらず、確認なしで**置換する（★2026-09-27 の決定）。1回が取り消しの1段。
  一覧は**作業リストのまま**（報告にならない）。置換した行はチェックボックスを失い、ほかの行は文字の今の位置へ移る。
  置換できない行（置換済み・ロック・検索の一致でない）では灰色。
  - 訂正:

- **REP-26** **ストーリーの行・文書の行の Replace**＝その中の**チェックした行**を置換する。
  ★**全部か無し** ―― 1行でも書けなければ全部戻す（2026-10-02 の決定＝そう言う）：
  `Replace: nothing was replaced - in 1 of these 2 checked row(s) the match ends an endnote, and InDesign's replace breaks an endnote there. Untick it and Replace again.`
  チェックした行が無ければ `Replace: no checked row in this story to replace - tick the rows first.`（メニューも灰色）。
  - 訂正:

- **REP-27** 行のメニューの Replace も、Change Checked と**同じ確かめ**をする（条件が変わった＝結果を消して断る・版・行の文字）。
  違いは、文書が変わっていたとき**アラートを出さず、メッセージ欄で断る**こと ――
  `Replace: the story of this row has changed since the search (edited or undone somewhere in it, not by KohakuFindChange) - search again.`／
  `Replace: the text of this row has changed since the search (edited, or undone) - search again.`
  ✅利用者に見える文の旧名は改名（2026-10-04）で `KohakuFindChange` に直した（"not by KohakuFindChange"）。
  - 訂正:

- **REP-28** 結果の文 ―― 1行＝`Replaced with Track Changes on - Reject Change on the row's right-click menu takes it back.`
  （脚注の中なら `Replaced (inside a footnote - Track Changes records nothing there, so it cannot be taken back with Reject Change).`）／
  複数＝`Replaced N checked row(s) of this story with Track Changes on - Reject Change on a row's (or its story's) right-click menu takes it back.`
  - 訂正:

- **REP-29** ★**Replace Again (Current Find/Change Settings)**（ストーリー・文書の行）＝**却下（Reject）で戻した行**を、チェックの有無にかかわらず、
  **今の**検索と置換の設定でもう一度置換する（名前は 2026-09-29 の決定＝それまでは Redo。「どの設定で書くか」を名前で言うため）。
  文字が変わった行は飛ばして数える。成功の文は `Replaced N row(s) of this story again with the current Find/Change settings (Track Changes on).`
  （断るときの文だけ頭に `Replace Again: ` が付く）。
  - 訂正:

- **REP-30** ★ヒットの行には Replace Again が無い（却下で戻した行はチェックボックスが戻るので、Replace か Change Checked で置換し直す＝2026-09-27 の決定）。
  - 訂正:

- **REP-31** 行のメニューの書き込みも、**窓・閉じる・「変更あり」の印**の扱いは Change Checked と同じ ――
  窓を付けるのは KFC が開いた章だけ／断られた・巻き戻った章は閉じて返す／巻き戻ったら印も入る前に戻す（2026-10-04 に直した＝GEN-16/17）。
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- REP-01：置換形式（Change Format）が書かれるか。本体の置換のコマンドに任せているので書かれるはずだが、この章のためには測っていない（GEN の未確認と同じ）。
- REP-16：英語 UI の取り消しの名前 `Replace` は文字列表の値で、実機の「編集」メニューで見てはいない。
- REP-26 の例文は、コードの組み立てから起こした文で、実機の表示を写したものではない（回帰の `story-replace-endnote-end` が実物を持っている）。
