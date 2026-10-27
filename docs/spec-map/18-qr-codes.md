# 18. QR コードの一覧（検索と置換の「QR Code」タブ・2.0）

> コード地図での位置: `model/KFCQRCodes.*`（QR を集める・読む）・`model/KFCQRSearch.*`（タブが今のタブか・一覧・行・Find Next）・`ui/KFCQRTab.*`（タブの面）・`ui/KFCJump.cpp`（クリックと Find Next の着地）。コード地図（`kbs-file-map.md`）ではブロック21（2026-10-27 に新設）。
> 設計書 `docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md`（Q1〜Q19・D1〜D8）・計画 `docs/superpowers/plans/2026-10-27-kfc-qr-code-tab.md`（Task 1〜8）・実測 `docs/ai-notes/kfc-qr-tab-measure-2026-10-27.md`（M1〜M10）。各項目の末尾の（Q<番号>）（D<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/qr-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 何ができるか

- **QR-01** ★本体の「検索と置換」のタブの行の末尾に、KFC の **「QR Code」タブ**が出る。そのタブが今のタブのとき、KFC の Find in … は、範囲の中の **QR コードを全部**一覧にする（絞り込みの欄は無い）
  （2026-10-27 作者「公式の検索ダイアログのタブにQRコードを追加、そのタブが選択されている時に、KFCで検索するとQRコードを検索できる」「すべてのQRコードを一覧にしたい」）。
  2.0 に入れた（版は 2.0.0 のまま・2.0.0 は未出品）。（Q1・Q2・Q3）回帰 `qr-tab-shown`・`qr-tab-find`
  - 訂正:

- **QR-02** ★タブの中身＝**Search:**（All Documents／Document＝本体の Search: と同じ並び）・**Find Next**・Find Next が言う2行（見つけた QR／選べない理由）。
  選択範囲は無い（作者「選択範囲 はいらないかも」）。**「完了」は本体の枠のもの**（自作のタブでも下に出ていて、押すと閉じる＝実測）＝KFC は置かない。
  KFC の一覧のボタンも置かない（一覧はほかのタブと同じく KFC のパネルの Find in …）。タブの文字は英語。
  ★並びは**本体の面と同じ**（2026-10-27 夜 作者「それのようにQRタブのほうもボタンの大きさや位置を合わせて欲しい」「検索：document のならびかたとか」）＝余白15・左に `Search: [Document]` を1行（ラベル55・リスト170）とその下に2行・右上に Find Next（本体の「次を検索」と同じ 116×26・同じ位置＝オブジェクトタブで測って合わせた）。（Q4・Q9・Q11・Q12）回帰 `qr-tab-shown`（面の部品）
  - 訂正:

- **QR-03** Search: の値は **KFC が覚える**（本体の検索と置換の設定は何も変えない）。InDesign を起動している間だけ覚え、起動した直後は Document。（Q19）回帰 `qr-tab-scope-all`
  - 訂正:

---

## 2. どのタブで探すか

- **QR-04** ★「QR のタブが今のタブか」は、**タブの面が受ける呼び出しから** UI の半分が持つ（窓は1つもたどらない）：
  面が見えている間（見えた〜隠れた）は QR・ダイアログを **QR のタブで閉じた**ら閉じている間も QR（KFC の Find は QR の一覧のまま＝開き直すと本体もそのタブで開く）・起動した直後は QR ではない。
  model はこれを UI の半分に聞く（UI が無い＝InDesign Server・別のビルドの UI＝QR ではない）。（D1）回帰 `qr-tab-switch-back`・`qr-tab-switch-race`・`qr-tab-closed`
  - ⚠QR のタブへ切り替えても、本体のモード（`GetSearchMode()`）は**前に見せていた本体のタブのまま**で、本体の通知も来ない（実測 M1）＝KFC のパネルのタイトルとアプリケーションバーの欄は、面が見えたときに KFC が書き直す。
  - ★★★面の「閉じる」呼び出しの中では、窓の一覧もダイアログの部品もたどらない（KT の面がそれをしたら、枠の完了で閉じたときに検索と置換が「閉じたのに残る」状態で固まった＝D1 の守り）。
  - 訂正:

- **QR-05** 保存したクエリを Run Saved Queries... のダイアログの **Find** で読み込むと、QR のタブは**身を引く**＝人が QR の面をもう一度見せるまで、KFC の Find は読み込んだクエリのタブで探す（保存したクエリは自分のタブを持つ）。
  検索と置換を QR のタブで閉じていたとき（そのままでは KFC の Find が QR の一覧のまま）も、開いて QR の面を見せているときも同じ。
  ⚠開いているダイアログは、クエリのタブが本体の今のモードと**同じなら QR の面のまま動かない**（Text のクエリ＝実測 10-27 夜）＝画面は QR Code タブ、KFC のパネルのタイトルはクエリのタブ（身を引いているので）。モードが違えば本体のタブへ移る。（計画 Task 4 の決まり）回帰 `qr-query-yield-closed`・`qr-query-yield-open`
  - 訂正:

- **QR-06** KFC のパネルのタイトル＝`Kohaku Find/Change - QR Code - Document`（Search: が All Documents なら `All Documents`・Book Scope ON なら `Book`）。面の Search: を変えるとすぐ付き直す。（設計書 4-2）回帰 `qr-tab-switch-back`
  - 訂正:

---

## 3. 範囲と集め方

- **QR-07** ★範囲 ―― **Book Scope ON**＝ブックの章を1つずつ（文字のタブと同じ・窓なしで開いた章は歩き終えたら返す・Find/Change Selected Documents (Book) も同じ）・開けなかった章は名指し（`1 chapter(s) could not be opened (qrC3.indd: file is missing).`）。
  **OFF**＝QR のタブの Search:（Document＝前面の文書／All Documents＝開いている文書を1つずつ・窓の無い文書も）。
  **Book Scope ON で Search: が All Documents なら断る**（文字のタブと同じ文＝FIND-13・範囲を利用者の後ろで読み替えない）。（Q5）回帰 `qr-list-book`・`qr-list-alldocs`・`qr-bookscope-alldocs-refused`
  - 訂正:

- **QR-08** ★**含める範囲のスイッチは無い**＝親ページ・ペーストボード・ロックした物・非表示の物・非表示のスプレッド・あふれた文字の中・グループやアンカー付きの枠の中の QR も全部並び、行の状態の語で見分ける。（Q6）回帰 `qr-list-places`・`qr-click-nest`・`qr-click-locked`
  - 訂正:

- **QR-09** 集め方＝**KFC 自身の object walker**（含める範囲を全部 ON・種類＝すべてのフレーム）で文書の枠を歩き、**枠のただ1つの子が QR の EPS**（`IID_IQRCODEDATAUID` を持つ `kEPSItem`）なら QR。本体の Object タブの設定も、本体の検索が使う共有の walker も使わない＝選択・画面・「変更あり」は動かず、アラートも出ない（実測 M8）。（D7）回帰 `qr-probe-kinds`・`qr-readonly`
  - 訂正:

- **QR-10** ★**読むだけ**＝一覧の検索は文書を「変更あり」にせず、本体の検索と置換の設定（どのタブも）を変えず、選択と画面も動かさない。（設計書 5章）回帰 `qr-readonly`
  - 訂正:

---

## 4. 行

- **QR-11** ★行の形は **Object タブの行と同じ**（OBJ-08）で、アイテム名の所が**種類と中身**：
  `[Missing ]P<ページ名>[(n)][ master][ grouped][ inline][ anchored][ hidden spread][ hidden][ locked]  ID<枠の UID>:<種類>  <中身>`（ペーストボードは頭が `pasteboard`・あふれた文字の中は `overset`）。
  文書（章）の行の下に直接並ぶ（ストーリーの段は無い）。ツールチップは無い（作者「ツールチップはいいです」）。（Q7）回帰 `qr-list-doc`・`qr-list-places`
  - 訂正:

- **QR-12** 種類の語と中身（作者「行にQRコードの内容を表示できると嬉しい」）：
  `Web`＝URL・`Text`＝本文・`SMS`＝`<番号>: <本文>`・`Email`＝`<宛先>  <件名>`・`vCard`＝`<名> <姓> / <組織>`（名と姓は本体の名刺の FN と同じ並び）。改行は空白にして1行。
  中身は 200 文字で切って `…`（文字の途中・サロゲートの間では切らない）・パネルではさらに幅で `…`。
  ⚠本体のホバーの文は使っていない（Email と名刺は改行入りの生の形式＝実測 M7）。（Q14・D6）回帰 `qr-probe-kinds`・`qr-row-long`
  - 訂正:

- **QR-13** 並び＝物の行と同じ（文書のページ順・親ページとペーストボードの物はその後）。上限＝1000 行（物の一覧と同じ・GEN-34）。（Q16）
  - 訂正:

- **QR-14** メッセージ欄＝`<n> QR code(s).`（ブックと All Documents は文字のタブと同じく `in <m> of <k> chapter(s).` などが付く）・上限で止まったら ` Stopped at the 1000 limit.`・0個は `No QR codes …`。最後に必ず ` Click a row to go to its QR code.`（置換の案内の代わり）。回帰 `qr-list-doc`・`qr-list-book`・`qr-none`
  - 訂正:

- **QR-15** 段の見出し＝`[QR Code]`（クエリが無いので、本体の語は付かない）。QR の一覧も1つの段になり、段の決まりは今のまま。回帰 `qr-tier-label`
  - 訂正:

---

## 5. クリック

- **QR-16** ★クリック（矢印で止まったときも）でその QR へ飛ぶ（作者「ジャンプされるのが重要」「閉じている章の QR にも飛ぶ」）＝**Object の行のクリック（OBJ-09）そのもの**：文書を前に出し（窓の無い章には窓を付け、閉じた章は開く）、スプレッドを出して真ん中へ、枠を選ぶ。
  ロック・非表示＝見せて選ばない（行の注で理由）・非表示のスプレッド＝何も動かさない・あふれた文字の中＝枠の＋へ（選ばない）。（Q8）回帰 `qr-click-nest`・`qr-click-book-closed`・`qr-click-locked`
  - 訂正:

- **QR-17** ★行の ID は **QR の枠の UID**（同じ枠に作り直しても変わらない）。クリックのときに枠がまだ QR か確かめ、枠が無い・もう QR でない（普通の画像に置き換えた）＝行は `Missing` になり、メッセージ欄に `Not found - the QR code is no longer in the document. Search again.`。
  中身が検索のときから変わっていても**行の文字はそのまま**（`Changed` は KFC が書いた行の語なので使わない・探し直せば新しくなる）。（Q15）回帰 `qr-click-deleted`・`qr-click-replaced-image`・`qr-click-undo-delete`
  - 訂正:

---

## 6. 一覧で効く操作と、灰色の操作

- **QR-18** ★効く＝クリック・矢印のジャンプ・段の見出しの **Search These Results Again**・**Clear These Results**・文書の行の **Search This Document Again**（どちらも、その一覧を作ったときの範囲で QR をもう一度集める）・段の切り替え。
  **灰色**＝Replace（右クリック・Return・Shift+Return）・Change All in Book (No List)・Special Operations・Create Outlines・Restore from Copy・Narrow Down・Search Text in These Objects（作者「置換はいいです」）。
  ショートカットなどで届いても model が断る：置換の類＝`… these rows are QR codes - nothing is changed from this list (click a row to go to its QR code).`／Narrow Down＝`a list of QR codes is not narrowed - it lists every QR code.`／Search Text in These Objects＝`these are QR codes - their frames hold no text.`／Change All in Book＝`Change All: the Find/Change dialog is on the QR Code tab. This panel lists QR codes there - nothing to change.`（Q13）回帰 `qr-menus-grey`・`qr-changeall-greyed`・`qr-list-again`・`qr-doc-again`
  - 訂正:

---

## 7. Find Next

- **QR-19** ★Find Next＝**本体の各タブの Find Next と同じ動き**（作者「次を検索 完了 ボタンが欲しい」）：押すたびに、タブの Search: の範囲で**次の QR を選んで見せる**（クリックと同じ着地）。
  並びは一覧と同じ（All Documents は文書も一覧と同じ順）。**起点は Find Next が最後に見つけた QR の次**（選択は見ない）・**最後の次は先頭へ黙って戻る**（本体の Object タブの実測 M6）。Book Scope は見ない（開いている文書だけ）。
  覚えはタブが見えている間だけ（タブを見せ直す・ダイアログを閉じると先頭から＝閉じた文書の番地は、後で開いた文書に使い回されうる）。一覧には触らない。（Q10・D5）回帰 `qr-findnext-order`・`qr-findnext-ignores-selection`・`qr-findnext-alldocs`
  - 訂正:

- **QR-20** 選べない QR は**アラートを出さず、タブの2行目で言う**（KFC の物の行と同じ扱い。1行目は見つけた QR＝中身が長ければその行だけが `…` で切れ、理由は切れない）：
  ロック＝見せて選ばない `That QR code is locked - not selected.`／非表示＝`That QR code is hidden - not selected.`／非表示のスプレッド＝何も動かさない `That QR code is on a hidden spread - not shown, not selected.`／あふれた文字の中＝枠の＋へ `That QR code is in overset text - not selected.`。
  1行目＝`<種類>  <中身> - page <ページ名>`（ペーストボード＝`- pasteboard`・あふれ＝`- overset text`・All Documents では後ろに `- <文書名>`）、2行目＝選べない理由（選べたときは空）。QR が1つも無い＝`No QR codes in document "<名前>".`／`No QR codes in <n> open document(s).`、文書が無い＝`No document is open.`、KFC の実行中＝`Another Kohaku Find/Change run is already in progress.`（行のクリックと同じく何もしない）。（Q10・D5）回帰 `qr-findnext-locked-hidden`・`qr-findnext-places`
  - 訂正:

- **QR-21** ★Find Next は選択と画面を動かす（それが目的）が、**文書を「変更あり」にせず、検索と置換の設定を変えない**。
  開いているダイアログは、選択や前面の文書が変わると本体のタブの Search: を選び直す（All Documents → Document など＝JMP の「検索:」を戻す）ので、Find Next の着地は**本体のタブ（QR のタブの前に見せていたタブ）の Search: を控え、1秒以内に選び直されたら戻す**（行のクリックと同じ仕組み・QR のタブでの行のクリックも同じ）。
  ⚠実測（10-27 夜）＝**QR の面が見えている間（とダイアログを閉じている間）は、本体は Search: を選び直さない**＝控えは保険（QR のケースの着地30回で一度も戻していない・武装した対照 `qr-findnext-native-scope-armed` は武装できず `unarmed-qr-cases.tsv`）。⚠閉じている間に All Documents にしても、**開いたとき本体が Document に戻す**（KFC のせいではない）。回帰 `qr-findnext-readonly`・`qr-findnext-native-scope`・`qr-click-native-scope`
  - 訂正:

---

## 8. ほかの入口

- **QR-22** アプリケーションバーの検索欄（Link the Application Bar's Search Field to This Panel・15 章）＝QR のタブのときは**空を映し**、Return は **QR の一覧**（打った文字は使わない＝Glyph・Object と同じ扱い）。（Q17）回帰 `qr-appbar-enter`
  - 訂正:

- **QR-23** クエリ＝**QR のタブはクエリにならない**（Run Saved Queries... の一覧に出ない）。QR のタブで本体のクエリ行の「保存」を押すと、名前の候補にタブの見出しが出るが、本体は**ファイルを書かない**（実測 M5）＝KFC は何も足さない。（Q18・D4）
  - 訂正:

- **QR-24** 完了＝**本体の枠の「完了」**。QR のタブで押しても閉じ、開き直せる（QR-04 の守り）。（Q11・D3）回帰 `qr-tab-done`
  - 訂正:

---

## ⬜ この章で未確認

- ✅（2026-10-27 夜）`qr-cases.tsv` 39/39 PASS（テスト用ビルド 22:02／UI 22:11）。⬜全件の回（ほかの表の悪化0）はまだ。
- ✅QR-05：同じモードのクエリでは、開いているダイアログは QR の面のまま（`qr-query-yield-open` の QRTABS）。
- ✅QR-21：本体は QR の面の間は選び直さない＝戻しの `kScopeCmdBoss` は一度も走っていない（QR の面から外れるかは試していない＝走らないので）。控えを外すかは別の回（作者の決まり＝書き終えた後の改良は別のセッション）。
- KFC の Minimizable／Translucent Find/Change と QR のタブを一緒に使ったときの大きさと見た目（設計書 4-6・計画 M9）は測っていない。
- QR-13：上限（1000 行）を踏む文書は試験では作っていない。
- QR-12：中身の 200 文字の切り口が結合文字・異体字セレクタの手前に来ないかは `KFCSafeCut` に任せていて、QR では試験していない（`qr-row-long` は絵文字＝サロゲートだけ）。
