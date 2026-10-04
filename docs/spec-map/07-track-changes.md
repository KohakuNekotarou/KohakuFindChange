# 07. 変更履歴 ― 署名・色・却下と承認

> コード地図での位置: ブロック **9b**（`model/KBSTrackChange.*`・`model/KBSSignRecordsCmd.cpp`）と **9**（`model/KBSReplaceEngine.cpp` の Reject／Accept）。
> 置換そのものは第6章、Show Changes（記録から一覧を作り直す）は第8章。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 記録の作り方 ―― 署名と時刻

- **TRK-01** 置換はすべて、**そのストーリーの変更履歴を ON にして**書く。もともと OFF だったストーリーは、書き終えたら OFF に戻す
  （ストーリーの設定は見つけたとおりに返す）。
  - 訂正:

- **TRK-02** ★記録の作成者は **`KohakuFindChange`**。**InDesign のユーザー名は変えない** ――
  利用者の名前で書いた直後に、その記録だけを `KohakuFindChange` に書き直す（次の行を書く前に）。
  （2026-09-27 の決定＝ユーザー名を切り替える方式は、スクリプトの DOM で「未設定」に戻せない名前が残りうるのでやめた）
  - 訂正:

- **TRK-03** ★記録の時刻＝**置換を始めた時刻（ミリ秒まで）＋行の番号×100ナノ秒**（2026-09-28 の決定）。
  この時刻で**1行ずつ見分ける** ―― 却下・承認・ジャンプ・Show Changes は、行の記録をこの時刻で探す。
  100ナノ秒の桁は、どのパネルにもスクリプトにも出ない（時刻の表示は変わらない）。
  - 訂正:

- **TRK-04** 記録は**文書に残す**（置換のたびに消さない）。これがあるから、後で却下でき、ジャンプが行を見つけ、Show Changes が一覧を作り直せる。
  - 訂正:

- **TRK-05** ★`KohakuFindChange` の変更は、その文書のユーザー一覧で **UI カラー Amber（琥珀色）** になる（GEN-18・2026-10-02 の決定）。
  それまでは一覧に名前が無く、白い地で描かれていた。
  - 訂正:

---

## 2. 記録の形

- **TRK-06** 1回の置換＝新しい文字に**「挿入」**の記録と、そのすぐ後ろに元の文字の**「削除」**の記録。空で置換したときは削除だけ。
  - 訂正:

- **TRK-07** ⚠前から順に書いた**隣り合う置換**は、行ごとの挿入は別々に残るが、**削除は1つにまとまり**、最後の行の時刻を持つ
  （InDesign が、接する削除を作者に関係なくつなぐため・実測）。
  ⇒ 隣り合う行は、却下・承認で**まとめて**動く（How to Use にも「隣り合っている置換は…纏まって戻ります」と書いてある）。
  - 訂正:

- **TRK-08** ★**脚注の中は記録されない**（InDesign が脚注の中の変更を記録しない・2026-09-26 実測）。置換はされるが、却下も承認もできない。
  行には `no track` が付き、右クリックすると理由がメッセージ欄に出る（ROW-29）。
  - 訂正:

- **TRK-09** 表のセルの中の変更は**記録される**。KFC は、表のセル・脚注・非表示の条件の中も含めて、ストーリー全体の記録を読む
  （スクリプトの `Story.changes` は本文の分しか数えないので、それより広い）。
  - 訂正:

- **TRK-10** ★**非表示の条件の中**の記録：条件を隠している間、記録は本文の外へ移る。KFC はそこにある記録も見つけるが、
  **却下・承認は断り**、「条件を表示してからもう一度」と言う（2026-10-02 の決定＝隠したまま戻さない・断って言う）。
  - 訂正:

---

## 3. Reject Change（却下）

- **TRK-11** 却下できるのは **KFC の置換の記録だけ**。メニュー（右クリック）――
  ヒットの行 `Reject Change by KohakuFindChange`／ストーリーの行 `Reject Changes by KohakuFindChange in This Story`／
  文書の行 `Reject All Changes by KohakuFindChange in This Document`／run の行 `Reject Changes by KohakuFindChange in This Run`。
  （名前に作成者を入れたのは 2026-09-29 の決定＝KFC の変更だけに効くことを名前で言う）
  - 訂正:

- **TRK-12** ヒットの行の却下は、その行に**接していて、まだ記録が残っている**置換済みの行も一緒に戻す（TRK-07 の理由）。
  記録がもう無い隣の行は連れて行かない（連れて行くと全体が断られて、その行がいつまでも戻せなくなったため＝2026-09-27 に直した）。
  - 訂正:

- **TRK-13** 却下は**全部か無し**で、**取り消しの1段**（「編集 > 取り消し」の名前は `変更を却下`／英語 UI は `Reject Change`）。
  先にすべての行の記録を探し、1行でも見つからなければ何も変えない。戻した後に元の文字が読み戻せなければ、全部巻き戻す。
  - 訂正:

- **TRK-14** 却下を断る理由と文 ――
  文書が開いていない＝`Reject Change: the document of this row is not open.`／
  記録が残っていない（取り消した・変更履歴パネルで承認か却下した・消した・脚注）＝`Reject Change: no tracked change of this replace is left for a row (…) - nothing was changed.`／
  削除の記録にほかの文字が混ざっている（接する隣の変更が承認された・ほかの人の削除がつながった）＝`Reject Change: the tracked deletion of this replace also holds text that is not these rows' (…) - nothing was changed.`／
  2行が同じ記録に当たった＝`Reject Change: two rows came to the same tracked change - nothing was changed. Search again.`／
  非表示の条件の中＝TRK-10。
  - 訂正:

- **TRK-15** 却下した行は**元の文字に戻り、チェックボックスが戻る**（作業リストでも、置換の報告でも）＝もう一度置換できる。
  メッセージ欄は `Rejected - the row is back to its original text. Tick it, or right-click it for Replace, to replace it again.`
  （接する行も一緒なら `the row and the matches touching it are back to their original text. Tick them, …`）。
  記録から作り直した一覧では行に `rejected` と付き、置換し直すには検索し直す（`… To replace again, search again.`）。
  - 訂正:

- **TRK-16** ストーリー・文書・run の行の却下＝その中の、**記録が残っている置換済みの行すべて**（脚注の中を除く）。
  `Rejected N row(s) of this story - back to their original text. Tick them and Replace to replace them again.`
  非表示の条件の中の行は戻さずに数え、条件を表示してから、と添える。
  - 訂正:

---

## 4. Accept Change（承認）

- **TRK-17** 承認できるのも **KFC の記録だけ**。メニュー ――
  ヒットの行 `Accept Change by KohakuFindChange`／ストーリーの行 `Accept Changes by KohakuFindChange in This Story`／
  run の行 `Accept Changes by KohakuFindChange in This Run`／文書の行 `Accept All Changes by KohakuFindChange in This Document`。
  - 訂正:

- **TRK-18** 承認は**確定** ―― その行は `accepted` になり、もう却下も承認もできない。
  `Accepted - the change by KohakuFindChange is final: it can no longer be rejected.`
  - 訂正:

- **TRK-19** 承認も却下と同じく、**接している行を一緒に**・**全部か無し**・**取り消しの1段**（`変更を承認`／`Accept Change`）。
  ストーリー・run の行は `Accepted N row(s) of this story - their changes by KohakuFindChange are final: they can no longer be rejected.`
  - 訂正:

- **TRK-20** ★文書の行の **Accept All** は、**`KohakuFindChange` の記録だけ**を承認し、**ほかの人の変更は残す**
  （2026-09-29 の決定＝それまでは本体の「すべて承認」と同じく、誰の変更でも承認していた）。取り消しの1段（`KohakuFindChange によるすべての変更を承認`）。
  - 訂正:

- **TRK-21** Accept All の件数は**置換の数**で数える（Show Changes と同じ数え方＝2026-10-04 から。それまでは記録の数で、2回の置換が「4」と出ていた）――
  `Accepted N change(s) by KohakuFindChange in the document - other changes are left. They can no longer be rejected.`
  非表示の条件の中は承認されずに残り、`… N left unaccepted - text under a hidden condition is left: show the condition and accept again.`
  承認された行は `accepted` になる。
  - 訂正:

- **TRK-22** Accept All のメニューが押せるのは、**文書が開いていて、`KohakuFindChange` の記録が1つでもある**とき。ブックの行では灰色（文書ごとの操作なので）。
  - 訂正:

---

## 5. 記録と行の結びつき

- **TRK-23** 却下・承認は**開いている文書でだけ**動く（閉じた章を開き直さない）。⇔ 置換やジャンプは、閉じた章をファイルから開き直す。
  - 訂正:

- **TRK-24** 置換した行は**記録の時刻で記録と結びつく**。そのため、置換の後に前後を編集しても、ジャンプ・却下・承認は記録の今の位置で行を見つける。
  - 訂正:

- **TRK-25** 変更履歴パネルで承認・却下した記録や、取り消した記録は、KFC からは「もう無い」。行はそのまま残り、その行の Reject／Accept は灰色になって、
  右クリックで理由が出る（ROW-29）。
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- TRK-02：記録を書き直したとき、本体の変更履歴パネルに出る「作成者」「時刻」が実際どう見えるかを、この章のためには見ていない。
- TRK-03：時刻を「最後に配った時刻の1ミリ秒後」にずらす場合（時計がまだ進んでいないとき）があるが、それが目に見える違いを生むかは確かめていない。
- TRK-20：ほかの人の変更が残ることは回帰試験（`accept-all-keeps-user`）が守っているが、この章のためには実機で見ていない。
- 英語 UI での取り消しの名前（`Reject Change` など）は文字列表の値で、実機の「編集」メニューでは見ていない。
- run の行の却下・承認の文は第8章で書く。
