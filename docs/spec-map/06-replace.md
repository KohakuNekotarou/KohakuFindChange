# 06. 置換 ― 行の Replace と Change All in Book (No List)

> コード地図での位置: ブロック **9**（`model/KFCReplaceEngine.*`＝行の Replace・`model/KFCChangeAll.*`＝Change All in Book と Clear Results）。
> 2026-10-06 夜の作者の決定（docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md 第5版・F16）で、一覧から書くのは**行の Replace だけ**になった（Change Checked・チェックボックス・Check All／Uncheck All・ストーリーと文書の行の Replace はやめた）。★その後 2026-10-17 から、行の右クリックの **Restore from Copy**（REP-38）・**Special Operations ▸ Create Outlines**（文字の行＝REP-39・物の行＝OBJ-19）・物の行のほかの特殊操作（OBJ-20）も一覧から書く（2026-10-19 ブロック5のコメントの回＝注の「the one write from the list」を外した）。番号は使い回さない。
> 変更履歴は利用者の設定のまま（第7章）、取り消し・やり直しへの追従は第9章。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 何で、どう置換するか

- **REP-01** 一覧から書くのは、**行の Replace（右クリックのメニュー／行を選んで Return キー）で選んだ行だけ**（1.4.0 から、まとめて選んだ何行か＝REP-36。それまでは1行だけ）。書く文字は本体の「検索と置換」の置換側
  （置換文字列／字形タブなら置換字形／文字種変換タブなら置換後の文字種）。
  置換文字列が**空なら一致を消す**（空でも灰色にしない＝空は「消して」という正しい頼み）。
  - 訂正:

- **REP-02** GREP の後方参照（`$1` など）やエスケープは **KFC が読まず、本体の置換がそのまま解釈する**（KFC は検索と置換の設定を読むだけで、書き換えない）。
  - 訂正:

- **REP-03** ★行の Replace は**1件ずつ** ―― その行のストーリーを、本体の「次を検索」のコマンドで一致へ進み、それがその行の一致なら、本体の置換のコマンドでその一致を置換する。
  ★この骨格は確定（2026-07-31・再設計を提案しない）。本体の「すべてを置換」は行の Replace には使わない＝その1行だけを渡す口が無く、置換後の範囲も返さないため。
  ブックの一致を一度に書くのは Change All in Book (No List)（REP-33）で、こちらは本体の「すべてを置換」そのもの。
  - 訂正:

- **REP-04** 行は「どのスレッドの、どこから、何文字」で見分ける。置換の途中で、置換そのものが新しく作った一致（検索では出なかったもの）は**飛ばす**。
  そのせいで行の一致に出会えなかった行は書かず、`missing` と言う。
  - 訂正:

- **REP-05** 歩く向きは前向き。★ただし GREP の検索文字列に `^`（段落の頭）があるときだけ、**書くときの歩きは後ろ向き**（2026-09-27 の決定）。
  検索と、書く前の照合は前向きのまま。
  ⚠1件ずつなので、`^` や前後読みのある GREP では、前の行を書くと後の行が同じ一致でなくなることがある＝その行の Replace は断る（何も書かない・`search again`）。
  行を置換する順番を変えれば本体の「すべてを置換」と同じ結果になる（回帰 `same-start-longer`／`same-start-longer-order`）。
  - 訂正:

- **REP-06** （2026-10-06 に外した＝第7章 TRK-26。それまでは「一致の周りの未承認の変更を先に承認してから置換」だった）
  - 訂正:

---

## 2. Change Checked の前の確かめ（2026-10-06 に外した）

- **REP-07** （2026-10-06 に外した＝Change Checked をやめた・F16。行の Replace の断りは §3・§4・REP-27）
  - 訂正:

- **REP-08** （2026-10-06 に外した＝Change Checked のメニューが無い）
  - 訂正:

---

## 3. 検索の条件が変わっていたら

- **REP-09** 検索と置換の**タブが検索したときと違う** → `The Find/Change dialog is on a different tab than when this search ran. Search again.`
  **結果は残す**（タブは1クリックで戻せるので）。
  - 訂正: ★v2（2026-10-14 夜・設計書 `docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md` 2-2・2-3）＝**その一覧が検索の条件の写しを持っていれば断らない**＝REP-10 の★のとおり、一覧の条件（タブごと）を一時的に入れて行の操作をし、終わったら「検索と置換」を元に戻す。この断りが残るのは写しの無い一覧（クエリの Run・Change All の一覧）だけ。

- **REP-10** ★**検索文字列・スイッチ**（大文字小文字・単語単位・かな・全角半角・範囲の5項目）・**検索形式**が変わった →
  **結果を消して**断る：`The Find/Change query has changed since this search - the results have been cleared. Search again.`
  （2026-08-03 の決定＝ほかの断りはすべて結果を残すが、これだけは例外。残しても、もう使えない一覧なので）
  - 訂正: ★v2（2026-10-14 夜・作者が承認＝設計書 `docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md` 2-3）＝**一覧が検索の条件の写しを持っていれば、消さずにその一覧の条件で行う**。行の Replace・Return・Shift+Return・まとめて選んだ行・物の行の確かめと置換・GREP の Preview Text・Search This Story／Document Again は、入口で「検索と置換」の検索側（タブ・署名・Find Format）が今の一覧と同じかを問い、**同じなら今までどおり**（置換側はダイアログの値＝検索の後で打った Change to が効く）、**違えば**一覧の条件を中断できるまとまりの中で一時的に入れて3重に検算（項目ごと・署名・段の見出しを読み返す）してから行い、終わったら「検索と置換」を元に戻す（一覧のタブが画面のタブと違えば2つのタブとも）。メッセージ欄の末尾＝`With this tier's query: <段の見出し> - Find/Change is as you left it.`。検算が合わない＝**行わず**元に戻す＝`Nothing was done: this tier's query could not be put into Find/Change (<何が>) - Find/Change is as you left it.`（<何が> を言う言葉の無い立ったエラーなら括弧ごと出さない＝2026-10-22 ブロック18 の監査）（戻せなければ `... Find/Change could not be put back as you left it (<何が>) - check Edit > Find/Change before replacing.`＝戻せなかったときの文はどこでもこの1つ＝Frames の一覧の書き込みも・2026-10-19 夜に作者「一つに統一」）。★R3＝一覧は**最後に置換に使った置換側を覚える**＝「検索と置換」のまま置換が通ったら写しを取り直し、今の段の見出しも `C:"<それ>"` に書き直す。この断り（結果を消す）が残るのは写しの無い一覧だけ。✅ 2026-10-14 夜に実機（ROW-42 の✅）。回帰の期待の書き直しが要る＝`ret-shift-refused`・`sr-query-changed-refused`・`ff-format-changed`・`ff-format-alldocs-close`・`dr-query-changed-refused`・`hd-ff-size-changed`・`hd-sw-case-changed`・`obj-refuse-query-changed`。

- **REP-11** 比べないもの ―― 検索の方向（KFC は前向きだけ＝GEN-22）と、**置換側**の設定（置換文字列など＝何が見つかるかではなく、何を書くかなので）。
  - 訂正:

- **REP-12** タブを確定し直せない → `The Find/Change tab could not be set - nothing was changed. Try reopening Edit > Find/Change.`（結果は残す）。
  - 訂正:

---

## 4. 文書が変わっていたら ―― 書く前の照合

- **REP-13** ★行の Replace は、その行の章を開き（閉じていれば開き直す）、**1文字も書く前に**照合する ――
  ①そのストーリーの版（変更カウンタ）が、KFC が最後に知っている版のままか
  ②行の文字（一致と前後）が、検索したときのまま読めるか
  ③同じ条件で歩き直すと、行の位置に同じ長さの一致が立つか。
  （★2026-08-10 の決定＝「この章は触られていないように見えるか」ではなく、**一致そのものの位置**で確かめる）
  ★行のテキストフォーカス（JMP-13）は置換では見ない（2026-10-08 夜の作者の決定「編集されていたら置換されないようにしたい」・回帰 `tf-replace-still-refused`）。行の場所が動くのは、KFC 自身の書き込み・その取り消しの追従・今の探し直し（JMP-14 の「ちょうど1つ」）のときだけ。
  - 訂正:

- **REP-14** （2026-10-06 に外した＝Change Checked の警告アラート「検索結果に変化を確認しましたので、置換を中止しました。」。行の Replace はアラートを出さずにメッセージ欄で断る＝REP-27）
  - 訂正:

- **REP-15** ★版を照合に使う理由 ―― 利用者の入力・ほかの操作の Ctrl+Z・変更履歴パネル・スクリプトで動いたストーリーには、**推測で書かない**
  （2026-09-29 の決定＝「安全第一」・検索し直してもらう）。KFC 自身の書き込みの取り消し・やり直しは追いかけているので、それで止まることはない（第9章）。
  - 訂正:

---

## 5. 書き込みと、その後

- **REP-16** 行の Replace は**1回が取り消しの1段**（「編集 > 取り消し」の名前はどの UI 言語でも `Replace`＝GEN-35）。
  Change All in Book (No List) は**ブック全体で1段**（`すべてを置換`／英語 UI は `Change All`）＝どの章を前にして Ctrl+Z しても、全部の章が一緒に戻る
  （章ごとの段にすると、取り消した章以外の履歴だけが消えて戻せなくなった＝2026-07-28 実測）。
  - 訂正:

- **REP-17** （2026-10-06 に外した＝Change Checked が章を全部開いてから書く決まり。Change All in Book は章を全部開いて持ってから1段で書く＝REP-33）
  - 訂正:

- **REP-18** ★Change All in Book の進捗バーの**キャンセルは全部を元に戻す** ―― 本文も、「変更あり」の印も、KFC が開いた章も。`Cancelled - nothing was changed.`
  - 訂正:

- **REP-19** （2026-10-06 に外した＝Change Checked の要約の数（ロック・`missing`・後注・本体が断った）。行の Replace は1行なので、書けなかったらその理由をメッセージ欄で言う＝REP-27）
  - 訂正:

- **REP-20** ★**後注の終わりで終わる一致も、本体と同じく書く**（2026-10-06・F12＝それまでは本体の置換が後注の範囲を壊すので書かなかった。本体の「すべてを置換」も同じく書く）。
  - 訂正:

- **REP-21** 行の Replace が脚注・表・アンカー付きオブジェクトを丸ごと消したとき、その中にあった行は**行き先を失う**（印は付かない）。
  - 訂正: 2026-10-08＝「一緒に消え、`deleted` が付く」は Change Checked の頃の決まり（付いたのは、置換を頼んだ行が同じ回のほかの行の置換で消えたときだけ）。1行ずつの置換では付く場面が無いので `deleted` ごと外した（盤面 `kfc-v13-review.md` P-1）。行き先を失った行に飛んだときの文は未測定。

- **REP-22** 終わったときの文 ―― 行の Replace＝`Replaced ID:<Story の UID> #<その Story の中で何番目の結果か>.`（例 `Replaced ID:262 #3.`＝一覧の Story の行と、その下の3つ目のヒット行。UID はスクリプトの `stories.itemByID` と同じ番号・ブックでは文書ごとの番号＝どの文書かは上の章の行。2026-10-07 作者の依頼「Return で次々に置換すると同じ `Replaced.` で分からなくなる」→作者の案「Story の UID＋何番目の検索結果」・表記は作者の「ID:262 #3」。それまでは `Replaced.`）／Change All in Book＝`N replaced in M chapter(s).`（何も無ければ `No match - nothing was changed.`）。
  ★Ctrl+Z で戻せることは書かない（2026-10-06 の決定・F20）。
  ★Change All in Book は、文と注（開けなかった章・窓なしで残った章）の**後ろ**に、**章ごとの内訳** `  By chapter: <章> (<数>), ….` を付ける
  （2026-10-07 の作者の追加「Change All in Book の結果に章ごとの内訳」）。書いた章だけ・ブックの順・**10章まで**、それより多ければ ` and <残り> more`。
  例 `3 replaced in 2 chapter(s).  By chapter: ch1.indd (1), ch3.indd (2).`
  最後に置くのは、メッセージ欄が4行（PNL-10）で、注を内訳で押し出さないため。何も書かなかったとき・キャンセル・失敗では付けない。
  - 訂正:

- **REP-22b** 選んだ文書だけのとき（SCOPE-26〜）の Change All の文は `N replaced in M of S selected document(s) (T in the book).`（何も無ければ `No match in the S selected document(s) - nothing was changed.`）。内訳は同じく後ろに付く。
  - 訂正:

- **REP-23** （2026-10-06 に外した＝Change Checked の後の「置換の報告」。一覧はいつも作業リストのまま）
  - 訂正:

- **REP-24** ★何も保存しない（GEN-17）。書いた章のうち **KFC が開いた章には窓を付け**、**何も書かなかった章は閉じて返す**。
  利用者が窓なしで持っている文書は隠したまま（` The document has no window - still hidden.`）。
  - 訂正:

---

## 6. 行の Replace

- **REP-25** **ヒットの行の Replace**＝その1行を、**確認なしで**置換する（★2026-09-27 の決定）。1回が取り消しの1段。（まとめて選んだ行の1つなら、選んだ行すべて＝REP-36。）
  一覧は**作業リストのまま**（報告にならない）。置換した行は書いた文字を見せ、ほかの行は文字の今の位置へ移る。
  置換できない行（置換済み・ロック・検索の一致でない）では灰色。
  - 訂正:

- **REP-26** （2026-10-06 に外した＝ストーリーの行・文書の行の Replace。F16）
  - 訂正:

- **REP-27** 行の Replace は**書く前に確かめる**（条件が変わった＝結果を消して断る・版・行の文字）。文書が変わっていたら**アラートを出さず、メッセージ欄で断る** ――
  `Replace: the story of this row has changed since the search - right-click the story row: Search This Story Again.`（後半の案内は 2026-10-09 から＝REP-35。同日の最終検査で縮めた＝前の文〈約200字〉はメッセージ欄の4行に入らず「…or ri…」で切れ、案内が読めなかった＝実測）／
  `Replace: the text of this row has changed since the search (edited, or undone) - search again.`
  書けなかった行は、理由を言って元のまま（`Replace: InDesign's replace command would not run there - left as it is.` など）。行の状態も元に戻る（語は付かない）。
  ✅利用者に見える文の旧名は改名（2026-10-04）で `KohakuFindChange` に直した（"not by KohakuFindChange"）。
  - 訂正: ★2026-10-14 夜の作者の判断で「ストーリーのどこかが変わったら書かない」は外れた（`7d4bddf`・計画 `docs/superpowers/plans/2026-10-14-kfc-row-focus-undo.md` Task 4）＝行は書く前に**フォーカス（一致の文字の位置）を追い**、追えた行は同じストーリーの別の所が変わっていても書く。最初の文（`the story of this row has changed`）で断るのは、フォーカスを追えないストーリー（検索より前の編集の Undo・閉じていた間に編集された章を開き直したとき＝回帰 `xs3-d2-para-save-close-jump-replace`）だけ。一致の文字が消えていた行は書かずに `Missing` になり（`Replace: it did not go through - nothing was changed.`）、その行の Replace は灰色（作者 2026-10-18「(a)」＝もとからこの形＝`KFCReplaceEngine::WhyGreyed`）。（2026-10-18 に地図へ追記＝回帰の期待もこの日に直した＝`stale-document`・`edit-elsewhere-then-change`・`tf-replace-still-refused`・`sr-refused-then-research`・`edit-shift-same-text`・`jump-after-edit`・`msg-story-changed-shot`／`-floor`＝`work/note-scripts/2026-10-18-kfc-narrow/renew_1018.py`。）

- **REP-28** 結果の文＝`Replaced ID:<UID> #<n>.`（REP-22）。
  - 訂正:

- **REP-29** （2026-10-06 に外した＝Replace Again (Current Find/Change Settings)。却下で戻す機能ごと外した＝第7章 TRK-28）
  - 訂正:

- **REP-30** （2026-10-06 に外した＝REP-29 と一緒）
  - 訂正:

- **REP-31** 行の Replace の書き込みの**窓・閉じる・「変更あり」の印**の扱い ――
  窓を付けるのは KFC が開いた章だけ／断られた・巻き戻った章は閉じて返す／巻き戻ったら印も入る前に戻す（2026-10-04 に直した＝GEN-16/17）。
  - 訂正:

- **REP-32** ★**行を選んで Return キー**（テンキーの Enter も）でも、その行を置換する（2026-10-06・F17＝矢印キーで行を歩き、合っている所で Return＝キーボードだけで置換できる）。
  右クリックの Replace と同じ口（同じ確かめ・同じ文）。Alt を押しながらの Return・ストーリー／文書／本の行の Return・置換済みの行の Return では何もしない（Shift を押しながらの Return は REP-32b）。**Ctrl を押しながらの Return は行を一覧から外す**（2026-10-18＝ROW-47・Ctrl+Shift+Return は何もしない。それまでは Ctrl+Return も何もしなかった）。
  1.4.0 から、ヒットの行を何行か選んでいれば、その全部を置換する（REP-36。選んだ行のどれも置換できなければ何もしない）。
  ダブルクリックでキーボードが本文に移っているときの Return は本文に改行を打つ（KFC は何もしない）。章を開いて書いたときも、キーボードはツリーに戻る。
  ★**Return で置換した後もキーボードはツリーに残る**＝続けて矢印で歩き、また Return で置換できる（2026-10-07・作者が見つけた不具合の直し＝それまでは Return の後に InDesign 自身がキーボードを文書へ返し、次の矢印が**レイアウトで選んでいるページアイテムを動かしていた**。右クリックの Replace では起きなかった）。KFC がツリーにキーボードがある間だけ Return を先に受け取り、InDesign のパネルの「Return で文書へ戻る」まで届かせない（`IEventDispatcher` に載せる小さな受け手）。
  ★「ツリーにキーボードがある」は**キーボードの枠（PNL-20）と同じ2つの条件**で聞く＝InDesign のキーボードの持ち主が一覧**かつ** OS がキーを送る窓がパネルの窓（2026-10-11 夜の再検査の K-1・それまでは持ち主だけを聞いていた）。
  ⚠2026-10-12 の実測＝「検索と置換」のタイトルバーを押した後（持ち主は一覧のまま・キーはダイアログへ）の Return は、**ダイアログ自身が受け取り、KFC の受け手には届かない**＝持ち主だけを聞いていた昔の形でも、行は置換されなかった。K-1 で心配した道は起きない（直しは枠と同じ問いに揃えたものとして残す）。回帰＝`ret-after-fc-title`／`ret-after-fc-title-armed`（後者は故障スイッチで昔の形にしても置換されないことの記録）。
  着地の最中の Return は何もしない（JMP-05b・2026-10-12）。
  ★**当たりの行が選ばれていないときの Return（ストーリー／文書／本の行）も、一覧が丸ごと受け取って何もしない＝キーボードは一覧に残る**（2026-10-28・出品の前に作者の依頼で測って見つけた不具合の直し＝`6fc5532`）。それまでは KFC が受け取らずに流し、InDesign がキーボードを文書へ返していた＝当たりの行のクリックで一致を選んでから文書の行へ移り（クリックか上矢印）Return を押すと、次に打った文字が文書の一致を置き換えた（`cat one cat two` が `x one cat two` に・KCM の Story の一覧で見つけたのと同じ形）。Shift や Ctrl を押しながらでも、当たりの行が選ばれていなければ同じ（置換も一覧から外すこともしない）。Alt を押しながらの Return と Ctrl+Shift+Return は今までどおり受け取らない。回帰＝`ret-branch-story-type`・`ret-branch-doc-type`・`ret-branch-up-type`（`retbranch-cases.tsv`）。全件の回 `1028b`＝770/770 PASS（出荷の回＝SDK フォルダの `docs/ai-notes/kfc-full-run-2026-10-28b.md`）。
  - 訂正:

- **REP-32b** ★**Shift+Return（テンキーの Shift+Enter も）＝置換して次へ**（2026-10-08・作者の決定＝本体の「置換」と「置換して検索」の2つのボタンと同じ分け方）。
  Return と同じ口でその行を置換し、**書けたときだけ**、その行より下で次の「置換できる行」（Replace が灰色でない行＝置換済み・ロック・検索の一致でない行は飛ばす）を選んで、矢印キーと同じようにジャンプする（閉じた章の行は開いて中へ入る・GREP の行なら Preview Text が出る）。キーボードは木に残る。
  書けなかったとき（断られた・巻き戻った）は動かない（理由をメッセージ欄で読めるように）。下に置換できる行が無ければ、置換した行に留まり、メッセージ欄の文の後ろに ` No row below can be replaced.` を付ける。
  「下」は木の上から下の順で、木を作っている階層の口（`ITreeViewHierarchyAdapter`）でたどる（結果のモデルの数え方ではたどらない＝木に無い行へは行かない）。
  Return（REP-32）は今のまま＝置換して、その行に留まる。検索の結果の文の最後の案内（FIND-34）にも、2026-10-09 から Shift+Return を書く（作者の依頼）。回帰＝`ret-shift`／`ret-shift-walk`／`ret-shift-skip`／`ret-shift-refused`／`ret-shift-book-closed`。
  何行か選んでいれば、その全部を置換して（REP-36）、選んだ行の**一番下の行**から次の置換できる行へ進む（進んだ先の1行だけが選ばれる）。（1.4.0）
  ★**押したまま（キーの自動反復）では1回だけ**（2026-10-09・ヘッダーの読み直し52の提案の案 A・作者の承認）＝反復のキーは受け取って、置換も移動もしない（それまでは反復のたびに置換して次へ進み、1秒押すと十数行が書かれる見込みだった）。Return も Shift+Return も同じ。たくさんを一度に書くのは Change All。アプリケーションバーの検索欄も、押したままの Return は1回だけ。回帰＝`ret-shift-held`（押し下げ3回で1行だけ）／`ret-shift-held-armed`（テスト用ビルドの故障スイッチ `return-repeat` で検査を外すと、何行も書かれる＝検査が効いていることの証拠）。
  - 訂正:

## 7. Change All in Book (No List) と Clear Results

- **REP-33** ★**Change All in Book (No List)**（パネルのメニュー）＝ブックの全章の一致を、**本体の「すべてを置換」で**一度に書く（2026-10-06・F18）。一覧は作らない。
  使えるのは **Book Scope が ON・対象のブックがある・パネルにヒットの行が無い・検索できるタブ・検索する文字か形式がある** とき（それ以外は灰色）。
  ★1.4.0 から **Object タブでも使える**（Find Object Format か検索のオブジェクトスタイルがあるとき・章ごとに本体のオブジェクトの「すべてを置換」＝OBJ-13）。灰色になるタブは Colour だけ。（1.4.0・17-objects.md）
  ヒットの行があるときは先に Clear Results（REP-34）。スクリプトなどから押されたら `Change All: the panel holds a list - choose Clear Results first.` と断る。
  （0件のブックの検索の後＝ブックの行だけのときは使える。）ブック以外（文書・ストーリー・選択範囲）は本体の「検索と置換」の「すべてを置換」を使う。
  ★**走り始めるとき（扉をすべて通った後）に、そのとき出ている結果を捨てる**＝0件の検索の `<ブック>  (0/0)` の行は消え、パネルは空になる（クエリ連続実行は 2026-10-31 から一覧を作る＝QRY-31。ここは今も作らない）。
  （2026-10-09 の最終検査 D9-3・それまでは行が残った。残った行は、書いた本の後でも "(0/0)" と読め、しかも KFC が覚える「検索したブック」は Change All のブックに替わっていた＝別のブックで走らせると、Change All のブックを閉じたときに「Results cleared - the book was closed.」で行が消え、行のブックを閉じても行は残った。回帰＝`ca-after-other-book-empty`。）
  章を全部開いて持ってから、**ブック全体で取り消しの1段**として書く（REP-16）。キャンセル・失敗は全部戻す（REP-18）。
  書いた章には窓を付け、何も書かなかった章は閉じて返す。何か書いたらパネルの絵が**鉛筆を持った猫**になる（次の検索で戻る）。
  Find/Change Selected Documents (Book) でブックパネルの一部の文書を選んでいる間は、その文書だけを書き、名前は `Change All in Selected Documents (No List)`（SCOPE-26〜・2026-10-07）。
  ★**探す所の無い章は、何も書かずに通る**（2026-10-12・実機で見つけた N-4）＝テキストフレームが1つも無い章（写真だけの章など）・マスターページにだけ文字があり Include Master Pages が OFF（既定）の章。
  本体の「すべてを置換」はそうした章にエラー無しで「失敗」と答え、それまでは `Stopped - nothing was changed: InDesign's Change All failed in <章>.` で**全部の章を巻き戻していた**（本体の `app.changeText()` はそうした文書を黙って通る）。今は「探し終えた・0件」として扱い、ほかの書かなかった章と同じく閉じて返す。
  （空のテキストフレームだけの章・一致の無い章は、もとから0件で通っていた。）回帰＝`ca-book-notext-chapters`（写真の章とマスターだけの章を飛ばして `3 replaced in 2 chapter(s).`）／`-armed`（テスト用ビルドの故障スイッチ `scope-walk-off` で昔の形＝写真の章で止まる）。
  - 訂正:

- **REP-34** **Clear Results**（パネルのメニュー）＝一覧を空にする（文書には触らない）。ヒットの行が無いときは灰色（0件のブックの行だけのときも）。文は `Results cleared.`
  ★**段を積んでいる間（ROW-39）は、ヒットの行が無くても押せて、段を消す**（2026-10-14＝積むのをやり直す口・ダイアログの Find の位置も1本目へ）。ヒットの行があれば一覧も消す。一致0の一覧（0件のブックの行など）は今までどおり残る（消すのは段だけ・文は `Results cleared.`）。
  KFC が窓なしで持っている章があれば、予約で閉じて返す（SCOPE-21）。
  - 訂正:

- **REP-35** ★**Search This Story Again**（ストーリーの行の右クリック・2026-10-09 の作者の決定）＝**そのストーリーだけ**を、検索のときの条件でもう一度歩き、その行を作り直し、ストーリーの版を覚え直す ―― 検索の後で直したストーリーでも、その行の Replace がまた書ける（REP-13 ①が通る）。
  そのストーリーの**置換済みの行は、目玉の無いものだけ消える**（検索がもう見つけないため・作者「よいで」。2026-10-17 の行の目玉から、目玉の付いた行は残る＝置換すると目玉が付く＝ROW-45）。ほかのストーリー・章の行と、その状態はそのまま。行は、そのストーリーの前の行があった所に入り、章はページの順に並べ直す（検索と同じ並べ方）。一覧の行の数の上限（300）はそのまま（入りきらなければ文で言う）。
  断る（一覧はそのまま・メッセージ欄で言う）＝実行中／検索の後で検索と置換の設定が変わった（`Search This Story Again: the Find/Change settings have changed since the search - search again.`）＝一覧が検索の条件の写しを持たないとき（クエリの Run・Change All の一覧）だけ。写しがあれば断らず、その一覧の条件で歩き直す（ROW-44・REP-10 の★v2＝2026-10-14 夜。2026-10-23 の全件テストの見直しでこの文を v2 に合わせた）。
  ★**検索が To End of Story・Selection だったときは、検索したその部分だけを歩き直す**（2026-10-09・作者の案「ストーリーの途中からの検索なら、その始める場所にテキストフォーカスを付けて覚えれば」・それまでは断っていた）。
  検索のときに本体が歩いた範囲（文字のかたまりの列＝表があるとその前・セル・後ろの3つ）を記録し、同じかたまりを公式の口（To End＝`QueryToEndOfStoryWalkerScope`・Selection＝`QueryFocusListWalkerScope`）で作り直せることをその場で確かめておく。範囲にはテキストフォーカスを付ける。
  歩き直すときの範囲の端＝ストーリーが記録した版のままなら記録した位置、変わっていればフォーカス（編集に付いてくる）から、**端のすぐ外の8文字が同じに読める一番近い所**（取り消しで古くなったフォーカスも、端のちょうどに打った文字も、これで正しい所に戻る）。
  断る＝範囲が離れたかたまり（2行以上にまたがる表のセルの選択）`... the searched part cannot be searched again on its own (separate stretches) - search again.`（2026-10-09 の最終検査で縮めた＝前の文は約190字で「search again」が切れた）／端のすぐ外の文字が書き換えられた `... the text at the edge of the searched part of this story has changed - search again.`。回帰＝`sr-toend-*`・`sr-selection-*`・`sr-table-*`。
  ★断っても、歩く前に行はフォーカスへ追ってある（行の文の読み直し・一致が書き換わった行に Missing）＝**一覧はそれを描き直す**（2026-10-19 ブロック7の再検査 D7-6＝それまでは描き直さず、次に描き直すまで前の文のままだった・回帰 `sr-toend-edge-repaint`／`-armed`）。
  KFC が検索の後で閉じた章は、窓なしで開いて歩き、閉じて返す。文は `Searched this story again: N hit(s).`（枠の一覧は `N frame(s).`・0件は `No matches.`・絞った一覧は枠の無いあふれの数とレイヤーの無い文書も言う＝Search These Results Again と同じ組み立て・2026-10-19 夜に作者「おすすめで」）。0件ならストーリーの行は消え、それで文書に行が1つも無くなっても文書の行は `(0/0)` で残る（ROW-07・2026-10-13 作者の決定 P-1）。行の並びが変わるので、それより前の KFC の書き込みの取り消しは一覧が追わなくなる（UNDO-14）。回帰＝`sr-*`。
  - 訂正:

## 8. まとめて選んだ行の置換と、文書の探し直し（1.4.0）

- **REP-36** ★**まとめて選んだ行は、まとめて置換する**（2026-10-10 作者の決定＝物の行・文字の行の順に。同じ日の夜にどの文書の行でもになった＝Change Checked の、チェックした行の置換を、選ぶ操作で作り直したもの）――
  何行か選んで（ROW-34〜38）**Return**（REP-32）か、選んだ行の1つの**右クリックの Replace**（ROW-32）。Shift+Return は全部を置換してから下へ進む（REP-32b）。確認は出さない。
  ★**取り消しの1段**（`置換`／`Replace`）。**文書が何個にまたがっても1段**＝どの文書で Ctrl+Z しても全部の文書が戻り、やり直しで全部書き直す（Change Checked と同じ作り＝文書をまたぐ段1つ。2026-10-10 夜〜10-11 未明の作者の決定・`5a2ef2a`）。一覧はそれに付いてくる（UNDO-02）。
  **文字の行**＝灰色の行（置換済み・Missing・ロック・本体が断った）は書かずに理由ごとに数え、残りは**全部か無しか**（1行のときと同じ確かめ＝REP-13 を全部の行に。1行でも変わっていれば何も書かずに断り、書いた後で1行でも書けなければ段ごと戻す）。
  断りの文は「a selected row」で言う（例 `Replace: the text of a selected row has changed since the search (edited, or undone) - search again.`・`Replace: a selected row's story has changed since the search - right-click the story row: Search This Story Again.`）。
  **物の行**＝1つの文書を本体のオブジェクトの検索で1回歩き、着いた物から書く。書けない行は理由ごとに数えて残し、書けた行は書く（全部か無しかではない）。本体がエラーで止まったら段ごと戻す（`Replace: InDesign's replace stopped with an error - nothing was changed.`）。最後に選んだ行の物を選んだままにする（OBJ-10 ⑨）。
  結果の文 ――
  文字の行 `Replaced 3 rows: ID:262 #1, ID:262 #3, ID:270 #1.`／残した行があれば `Replaced 2 of 3 rows: ID:262 #1, ID:262 #3. Left as they were: 1 locked.`（数える語＝`already replaced`／`missing`／`locked`／`refused`・この順に1つ＝2026-10-10 の作者の決定 D-5＝灰色の行を行の状態ごとに数える・`docs/ai-notes/kfc140-diff-cleanup-2026-10-10.md`）／
  文書をまたげば `Replaced 3 rows in 2 documents: a.indd ID:262 #1, ID:262 #3; b.indd ID:270 #1.`／
  物の行 `Replaced 3 objects: ID:259, ID:260, ID:261.`・`Replaced 3 of 5 objects: ... Left as they were: 1 locked, 1 changed since the search.`・`Replaced 3 objects in 2 documents: a.indd ID:259, ID:260; b.indd ID:12.`。
  名前を挙げるのは8行（8個）までで、それより多ければ `, ...`。書いた文書のどれかが窓の無い利用者の文書なら、後ろに ` A document written has no window - still hidden.`（1つの文書なら ` The document has no window - still hidden.`＝REP-24）。
  選んだ行のどれも置換できなければ Replace は灰色・Return は何もしない（ROW-32）。回帰＝`ms-cases.tsv`・`sel-cases.tsv`・`ov-cases.tsv`。（1.4.0）
  - 訂正:

- **REP-37** ★**Search This Document Again**（文書の行の右クリック・2026-10-10 作者の依頼＝ブックなど複数の文書の検索で、Object タブもほかのタブの検索も）＝**その文書を、検索のときの条件で丸ごともう一度歩き**、その文書の行を作り直し、ストーリーの版を覚え直す
  ―― 検索の後で直した文書でも、その行の Replace がまた書ける。Search This Story Again（REP-35）の文書版。
  使えるのは**文書を丸ごと探した検索**（ブックの章・All Documents の文書・「検索:」ドキュメント）だけ。★**文書の一部を探した段（「検索:」ストーリー・ストーリーの最後まで・選択範囲）では灰色**（2026-10-17 夜の作者「灰色でもいいかも」＝その段はストーリーの行と段の見出しで、最初と同じ条件で探し直す＝REP-35・ROW-44）。スクリプトから ID で押されたときだけ、下の断りの文が出る。Object タブの検索でも（本体のオブジェクトの検索で歩き直す・利用者の選択は戻す＝OBJ-06）。
  その文書の置換済みの行は、目玉の無いものだけ消え（検索がもう見つけないため・目玉の付いた行は残る＝ROW-45）、ほかの文書の行と、その状態はそのまま。一致が0件になっても文書の行は `(0/0)` で残る（ROW-07）。一覧の行の数の上限（300）はそのまま（入りきらなければ文で言う）。
  進捗バー（Cancel つき）を出す＝その間は検索と同じく、ほかの実行を断る（GEN-33）。KFC が検索の後で閉じた章は、窓なしで開いて歩き、閉じて返す。
  断る（一覧はそのまま）＝実行中／検索が文書の一部だった（灰色の項目をスクリプトが押したとき）`Search This Document Again: the search was over part of the document (Search: Story) - search again.`（`To End of Story`・`Selection` も）／
  検索と置換の設定が変わった `Search This Document Again: the Find/Change settings have changed since the search - search again.`／開けない `...: the document of this row could not be opened.`／
  Cancel `...: cancelled - the document's rows stay as they are.`／本体の検索がエラーで止まった `...: InDesign's search stopped with an error - the document's rows stay as they are.`（行の文字は印へ追った後の形＝「前のまま」とは言わない・2026-10-19 夜）。
  ★Cancel・エラーでも、歩く前に行はフォーカスへ追ってある＝一覧はそれを描き直す（REP-35 と同じ・2026-10-19 D7-6）。
  文は `Searched this document again: N hit(s).`（Search This Story Again と同じ組み立て＝枠の一覧は `N frame(s).`・0件は `No matches.`。上限に当たれば ` The list's limit of rows was reached - its other matches are not listed.`）。行の並びが変わるので、それより前の KFC の書き込みの取り消しは一覧が追わなくなる（UNDO-14）。回帰＝`dr-cases.tsv`（`dr-part-refused`＝2026-10-17 から灰色を期待）。（1.4.0）
  - 訂正:

- **REP-38** ★**Restore from Copy**（文字の行の右クリック・Replace の下・2026-10-17 作者の依頼「コピーで戻すは右クリックメニューで」・名前は同じ夜の作者「Copy で戻すというのにしてほしい」＝最初の名前 Restore Original から改名＝作者の不在中に私のおすすめで作り、その夜に作者の決定で直した形＝設計書 `docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md` の C1〜C16）＝**置換した文字の行を、置換の前の文字に戻す**――
  文字の行の置換（右クリックの Replace・Return・Shift+Return・まとめて選んだ行＝REP-25・REP-32・REP-36）は、**書く直前に**一致の写しを KFC 専用の置き場（文書ごと・クリップボードは使わない・文書の取り消しの段には入らない）へ取っておく。一致が⏎をまたぐときは、一致が終わる段落の⏎の手前まで写す。
  Restore from Copy はその写しを、置換で書いた文字（と広げた後ろ）の上に貼る＝一致の中の脚注も戻り、⏎をまたいだ一致は2段落目の段落の属性も戻る（置換の前と構造で同じ＝KIDMCP の task_diff で確かめた）。
  **置換が段落の書式を当てていたら**、写すときに一致が始まる段落の段落スタイルと上書きも控え、戻した後でその段落に当て直す（C16）。⚠段落の書式は段落に1つ＝同じ段落のほかの置換済みの行の置換で当たった書式も置換の前に戻る（作者の承認）。
  **ハイパーリンク**＝リンクの中の一致・リンクの端をまたぐ一致・一致の中のリンクは、戻したときにリンク1本（名前・行き先そのまま・範囲も置換の前）になる（C15＝貼り付けが作る重複を消し、元の範囲を付け直す）。一致の中のリンクは2つの形＝置換が消す所（一致の後ろの、置換が消す側）にあれば写しが同じ名前で戻し、置換が書いた文字の上に残す所にあれば、貼る前に貼る範囲の横の空いた1字へよけて、貼った後で元のリンクを元の範囲に付け直す（元のリンクの物・名前・行き先のまま＝2026-10-19 ブロック9の不具合の回 `2e34c14`）。
  まとめて選んだ行の1つなら、選んだ行すべて（置換していない行は数えて残す）。同じ段落の2行は、どちらの順で置換していても周回で両方戻る。置換の後に同じストーリーの別の所を直していても、行のフォーカス（JMP の自前のフォーカス）で追った今の場所に戻す。
  押せる＝写しを持つ置換済みの文字の行（`Changed`）があるとき・**灰色＝置換していない行と写しの無い行だけのとき**（作者「実行できないときは色を変えられるとうれしい」・SET-11）。物の行のメニューには無い（OBJ-19 のメニュー）。
  戻すと＝`Changed` が消え、目玉は外れる（ROW-45）、Replace がまた押せる。取り消しの1段（Edit メニューの名前 `Restore from Copy`）＝Ctrl+Z で `Changed` に戻り、Redo で戻した姿（UNDO-02）。写しは消えない（Ctrl+Z の後にまた戻せる）。
  ★**表のセル・入れ子の表のセル・脚注の中の一致も写しを取って戻す**（2026-10-30・設計書 `docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md` C＝作者が選んだ「判断待ち⑤」）＝写す範囲と貼る範囲は**一致が始まる位置のスレッドの中**（本文・表のセル・脚注のスレッド＝`KFCCopyStore::CopyableThread`・そのスレッドの終わりの1字の手前まで）＝⏎の手前まで広げるのも、そのスレッドの中で止まる（セルの最後の段落はスレッドの終わりの手前まで）。KT の 2026-10-24 の実測（1つのスレッドに収まる範囲は写して貼り戻しても落ちない・文字も書式も1文字ずつ同じ）の上に乗せた。それまでは本文の主スレッドだけ。回帰＝`nor-cases.tsv` の `restore-in-*`・`restore-cell-*`・`restore-footnote-last-para`。
  **写しを取らない**（行は灰色・押せば理由を言う）＝ノートの中の一致（測っていない・⚠KFC の検索はノートの文字に届かない＝行が出ない・本体の検索と置換も数えない）・スレッドをまたぐ範囲（文は `its match is in a note, or runs out of the text it stands in.`）／**一致がアンカー付き・インラインの物を含む**（作者「UID が変わるのが問題」＝写しで戻すと別の UID の物になる）／**貼り付けがリンクの元の文字を丸ごと置き換える形**（一致そのものがリンクの元・広げた後ろにリンク・一致の端に接したリンク＝付け直しで InDesign が落ちた・リンクを UID ごと戻す案は**やらない**＝2026-10-24 作者「やりません」）と相互参照／広げる後ろに物の文字や印がある／写せなかった。
  押した後に断る（行はそのまま・何行なら数えて言う）＝置換で書いた文字を後で直した `edited since`・広げた後ろが変わった `rest of paragraph changed`・ストーリーを追えない `not followed`・ロック `locked`・文書を開けない・**貼り付けがリンクの元の文字を丸ごと置き換える** `hyperlink in the way`（置換の後に書いた文字の上に作ったリンク・よける横の字が無い一致の中のリンク＝貼れば文字の無い壊れたリンクが残る）・**戻す段落スタイルが置換の後で削除された** `paragraph style deleted`（消えたスタイルを本体に渡さない＝守り・壊れる側は回していない）（2026-10-19 `2e34c14`）。閉じたブックの章は、置換と同じく窓なしで開いて戻し、窓を出す。
  文＝`Restored ID:262 #3.`／`Restored 2 of 3 rows: ID:262 #1, ID:262 #3. Left as they were: 1 edited since.`／`Restored 3 rows in 2 documents: a.indd ID:262 #1; b.indd ID:270 #1.`／1行の断り `Restore from Copy: <理由>`（例 `Restore from Copy: no copy was kept when this row was replaced - its match holds an anchored object, which a copy would put back as a new object (a new ID).`）。
  写しの置き場は InDesign を終えるまで残る（保存していない文書の分は閉じたときに捨てる）・キャッシュフォルダに1つ約120KB のファイル（`KohakuFindChangeCopyStore<番号>`）。実機＝設計書 §7（テスト用ビルド・KIDMCP）。回帰＝`restore-from-copy`・`restore-two-rows`（まとめて選んだ2行）・`restore-one-edited`（書いた文字を後で直した行＝1行の断り）・`restore-two-one-edited`（2行のうち直した1行を数えて残す）＝2026-10-19 に合格（`tier-cases.tsv`）。同じ日（ブロック9の監査）に＝`restore-para-style`（置換が段落スタイルを当てた行＝段落のスタイルと上書きが戻る）・`restore-run-on`（⏎をまたぐ一致＝2段落目の揃えが戻る）・`restore-link-around`／`restore-link-edge`（リンクが一致を囲む／一致の端をまたぐ＝リンク1本で元の範囲）・`restore-link-whole`／`restore-link-rest`（一致そのものがリンク／後ろにリンク＝写しを取らない＝灰色）＝合格・`restore-link-inside`＝下の⬜→直した。同じ日（ブロック9の不具合の回）に＝`restore-link-inside-named`／`-hidden`（隠れた行き先）／`-para`（一致が段落まるごと＝よける字は⏎）／`-undo`（Ctrl+Z で置換の後の姿・Redo でまた1本）・`restore-link-tail`／`-hidden`（置換が消す所のリンク）・`restore-link-made-after`（置換の後に書いた文字の上に作ったリンク＝断る）・`restore-para-style-deleted`（戻す段落スタイルを削除＝断る）・`restore-anchor-made-after`／`restore-anchor-point-made-after`（テキストアンカーは文字（U+FEFF）＝書いた文字の前なら戻す・中なら edited since）＝合格・前の形を捕まえる `restore-link-inside-named-armed`／`restore-link-made-after-armed`（故障スイッチ `restore-link-in-way`）。（1.4.0）
  - 訂正:

- **REP-39** ★**文字の行の Special Operations ▸ Create Outlines**（ヒットの行の右クリック・Restore from Copy の下・2026-10-18 作者「Text 一致の文字だけを Create Outlines。テキスト用のスペシャルの機能ですね（オブジェクト以外用）」＝設計書 `docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md`）――
  **一致の文字だけ**をアウトラインにする（本体の 書式 > アウトラインを作成 と同じ命令）。まとめて選んだ行の1つなら選んだ行すべて・文書をまたいでも1つの取り消しの段（Edit メニューの名前 `Create Outlines`＝UNDO-02）。
  本体はアウトラインを**行（語のかたまり）ごとに1つのインライン**にする＝2行にまたがる一致は2つのインラインと間の空白、段落をまたぐ一致は段落の区切りも残る、一致の中の元のインラインは残る（設計書 §3-1 の M7＝DOM の `createOutlines(true)` で実測）。
  書いた行は `Outlined` と読み、一致の位置には**元の文字**を描き続ける（本文はもうインラインの印）。目玉が付く（ROW-45）。同じストーリーの後ろの行も位置を追う（行の長さは書く前と後のストーリーの長さの差で読み直す）・アウトラインにした行は、後の書き込みでも運ばれる（`b3ca26e`）。**置換済みの行もアウトラインにできる**（書いた文字をアウトラインにする）。
  **灰色**＝押せる行が1つも無いとき（SET-11）。1行ずつの扉＝物の行（その Create Outlines は OBJ-19）・アウトライン済み・文字の無い一致（GREP の `^`・`$`）・あふれた文字（本体が断る＝エラー 80650）・ロック（ロックしたレイヤー・ストーリー＝DOM は通すが KFC は Replace と同じく断る）・文書が無い／開けない・ストーリーが追えない形で変わった・一致の文字が検索の後で書き換わった（置換済みの行は書いた文字と比べる）。
  文＝`Outlined ID:262 #3.`／`Outlined 2 of 3 matches: ID:262 #1, ID:262 #3. Left as they were: 1 in overset text.`／`Outlined 3 matches in 2 documents: a.indd ID:262 #1, ID:262 #3; b.indd ID:270 #1.`／1行の断り `Create Outlines: <理由>`（例 `Create Outlines: the match is in overset text, which InDesign cannot outline - left as it is.`）。
  ✅ 実機（2026-10-18・テスト用ビルド・KIDMCP・`work/note-scripts/2026-10-18-kfc-narrow/x1.ps1`）＝1行（`Outlined ID:254 #1.`・Ctrl+Z・Redo）・同じ段落の2つ目を続けて（1つ目の行も読み直される＝直したのは `b3ca26e`）・2つの文書の行をまとめて・あふれの行の項目は灰色・置換済みの行もアウトラインにできた。⬜まだ＝表のセル・脚注・後注の中の一致（DOM では通った＝M7）・ロックしたレイヤー（断る文）・アンカー付きの物を含む一致・連結した枠をまたぐ一致・「検索と置換」を開いたまま。回帰＝`outline-text-row`・`outline-two-rows`（まとめて選んだ2行）・`outline-one-edited`（一致の文字を後で直した行＝1行の断り）・`outline-two-one-edited`（2行のうち直した1行を数えて残す）＝2026-10-19 に合格（`tier-cases.tsv`）。（1.4.0）
  - 訂正:

- **REP-40** ★**文字の行の Special Operations ▸ Clear Overrides**（ヒットの行の右クリック・abc 順で Create Outlines の上・2026-10-30＝設計書 `docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md` B2〜B7＝作者「設計は貴方のお勧めで」の私の判断）――
  **本体の段落スタイルパネルの「オーバーライドを消去」を、その一致を選んで押したのと同じ**＝一致の文字の文字の上書きと、一致がかかる段落の段落の上書きを消す（段落の上書きは段落まるごとに効く＝本体と同じ・同じ段落のほかの行にも効く・**一致の無い隣の段落は、同じ上書きを持っていても触らない**＝本体と同じ・2026-10-30 のブランチの見直しで直した＝段落の属性の帯は同じ属性の段落が続くと1つの run にまとめるので、run の範囲で消すと隣まで消えていた・回帰 `overrides-clear-para-neighbour`）。書く口＝公式の `ITextModelCmds::ClearOverridesCmd`（消す属性の名前を `IAttributeStrand::GetLocalOverrides` で集める）。**ルビと条件は消さない**（本体はどちらも上書きと数えない＝ROW-46 の B0・DOM の `clearOverrides()` も残す＝条件は文字に「スタイルの上書き」として載っているので、倍率などと一緒に消えていた＝2026-10-30 のブランチの見直しで直した・回帰 `overrides-clear-twin-mixed`）。**本体が上書きなしと言う一致は書かずに数える**（`had no overrides`）。
  まとめて選んだ行の1つなら選んだ行すべて・文書をまたいでも1つの取り消しの段（Edit メニューの名前 `Clear Overrides`＝UNDO-02）。書いた行は頭に `Cleared`（物の行の Clear Object Style Overrides と同じ語）・目玉が付く（ROW-45）。文字は変わらないので Replace は灰色にならず、ほかの行も動かない。置換済みの行も消せる（Restore from Copy は写しの書式＝上書きごとに戻す）。後で Replace／Restore from Copy をかけると語はその書き込みの語に替わる。
  **灰色**＝押せる行が1つも無いとき（SET-11）。1行ずつの扉＝物の行・アウトライン済み・文字の無い一致（GREP の `^`・`$`）・ロック・文書が無い／開けない・ストーリーが追えない形で変わった・一致の文字が検索の後で書き換わった（置換済みの行は書いた文字と比べる）。**あふれた文字は消せる**（組版が要らない）。
  文＝`Cleared overrides on ID:262 #3.`／`Cleared overrides on 1 of 2 matches: ID:262 #1. Left as they were: 1 had no overrides.`／1行の断り `Clear Overrides: <理由>`（例 `Clear Overrides: the match has no overrides to clear - left as it is.`）。
  回帰＝`nor-cases.tsv` の `overrides-clear-*`（★本体と双子で並べる＝`overrides-clear-twin`＝双子の段落に DOM の `clearOverrides(OverrideType.ALL)` を当てて、属性を1つずつ比べる・`overrides-clear-twin-mixed`＝本物の上書き〈倍率〉と、上書きでないもの〈条件・ルビ・文字スタイル〉が同じ一致に付いた形・`overrides-clear-in-cell`＝表のセルの一致〈段落の範囲はセルの終わりまで・隣のセルは触らない〉・`overrides-clear-para-neighbour`）。脚注の中の一致は試験していない（セルと同じ読み方＝段落の範囲はそのスレッドの中）。（作り＝`model/KFCTextOverrides.cpp`・`KFCReplaceEngine::ClearOverridesHits`。）
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- ✅（2026-10-19 ブロック9の監査の新しいケースで見つけた・同じ日の「9 不具合」の回で直した `2e34c14`＝下の形の元のリンクは、貼る前に横の1字へよけて付け直す・実機で RED→GREEN）REP-38：**一致の中のリンクを置換が消さずに書いた文字の上に残す形**（`the cat sat one` の `at sa` にリンク・`cat sat`→`dog`＝置換は文字を上書きで書き、リンクは `og` に残った）では、Restore from Copy の後にリンクが2本になる＝写しが持ち込んだリンク（名前 `L1 1`・元の範囲）と、文字を失った元の `L1`（REP-38 の「リンク1本」と違う）。10-17 の実測（H4＝中のリンクは置換が消す）は、リンクが書いた文字より後ろ（消える側）にある形だった見込み。回帰＝`restore-link-inside`（RED・期待は REP-38）。

- ✅（2026-10-11 全件の点検で確かめた）REP-01：置換形式（Change Format）は書かれる＝回帰 `hd-cf-char`・`hd-cf-formatonly`・`hd-cf-formatonly-undo`（本体の「すべてを置換」と同じ結果）。
- ✅（2026-10-13 実機で確かめた）REP-16：取り消しの名前は日本語 UI でも `Replace`（行の Replace の後の `app.activeDocument.undoName`＝探りの表 `work/kbs-regress/archive/probe-undoname-1013.tsv`）。「編集」メニューの絵では見ていない。Change All・Run Queries の名前と進捗バーの題は同じ仕組み（文字列表のキー）で、実機では読んでいない。
- ✅（2026-10-11 全件の点検で確かめた）REP-32：矢印キーで行を歩いてから Return で置換する流れは、回帰 `ret-walk`（`click:1;key:ENTER;key:DOWN;key:ENTER`）・`ret-book-walk`・`ret-walk-item` で回っていて合格している（KIDMCP の `send_input` は矢印を送れる）。
- （2026-10-14・ブロック9の不具合の再検査 D9-2・守りだけ）**閉じた章を開く間に、先に届けた文書が閉じたら**＝開くとメッセージループが回る（そこで Return が届くことは 10-12 に実測＝N-1）＝閉じる操作も届くなら、まとめて選んだ文字の行・物の行の置換は閉じた文書を確かめ・書こうとして落ちうる（未実測）。⇒届け終えた後にすべての文書がまだ開いているかを1回聞き、閉じていれば何も書かずに `Replace: a document of the selected rows has been closed - nothing was changed.`。Change All in Book は、クエリの実行と同じく章ごとに書く前に聞き、閉じた章は書かずに飛ばす（言わない）。⬜実機＝閉じる操作がそこで届くかを測る（KIDMCP の `send_input` で、窓のある章を閉じるクリックを閉じた章が開く間に送る）。
