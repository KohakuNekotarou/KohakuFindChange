# 17. オブジェクトの検索（Object タブ・1.4.0）

> コード地図での位置: `model/KFCObjectSearch.*`（範囲・歩き方・行・指紋・後始末）・`model/KFCObjectReplace.*`（行の置換・文書ごとの Change All）・`ui/KFCJump.cpp`（クリック・行の置換の前後）。コード地図（`kbs-file-map.md`）のブロック番号はまだ振っていない。
> 設計書 `docs/superpowers/specs/2026-10-09-kfc-object-search-design.md`（O1〜O15）・計画 `docs/superpowers/plans/2026-10-09-kfc-object-search.md`（訂正 1〜10）。各項目の末尾の（O<番号>）は設計書の決定。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 何ができるか

- **OBJ-01** ★「検索と置換」が **Object タブ**のとき、文字のタブと同じ4つが使える ――
  ①一覧（Find in …・ブックも）②行の置換（右クリックの `Replace`・Return・Shift+Return）③Change All in Book (No List) ④Run Saved Queries... のクエリの実行
  （2026-10-09 作者の依頼「ver1.4.0として、オブジェクトの検索を追加したい」・4つとも作者の選択）。Colour タブは今どおり断る（FIND-07）。（O1）
  - 訂正:

- **OBJ-02** ★**合うかどうかを決めるのも、書くのも InDesign 自身**（本体の「検索と置換」のオブジェクトの検索）。
  KFC は、本体のオブジェクトの検索が歩く道（共有の object walker）を**1つの文書に向けてから**呼ぶだけ ―― 窓の無い章も、そのまま探せる（実測）。
  自前の照合はしない（本体と結果がずれないように＝作者「案Cがいい」）。（O2）
  - 訂正:

---

## 2. 範囲と、何が並ぶか

- **OBJ-03** 範囲 ―― Book Scope ON＝ブックの章を1つずつ（文字と同じ。窓なしで開いた章は歩き終えたら返す）。
  OFF＝Object タブの「検索:」に従う：ドキュメント＝前面の文書／すべてのドキュメント＝開いている文書を1つずつ（窓の無い文書も）／
  選択範囲＝**検索を始めたときに選んでいたページアイテムだけ**。
  ★**文字にカーソルがある（キャレット・文字の範囲を選んでいる）ときは、そのカーソルがあるフレーム**（2026-10-10 作者の依頼「Indesignの検索ダイアログの方では、カーソルが立っているオブジェクトが選択されていると判断されて検索がされる、同じようにして欲しい」）
  ＝本体に「検索:」から歩く道を作らせる（本体の「次を検索」と同じ＝実測 M6「キャレット→その枠だけ」）。回帰＝`obj-selection-caret`／`obj-selection-text`（A3 の文字にカーソル→A3 だけ・カーソルは残る）。
  ⚠数えるのは**レイアウトの文字のカーソルだけ**＝ストーリーエディターやノートの中のカーソルは今は数えない（下の「断る」になる）。本体の「次を検索」がそれを数えるかは**未測**（2026-10-12 の不具合の再検査 O-1）＝数えるなら KFC も数える（上の作者の依頼どおり）。
  「検索:」は**設定に入っている値**で読む（ダイアログが開いていると表示が設定と違うことがあり、本体のオブジェクトの検索も設定の値で歩いた＝実測 M6）。未設定はドキュメント。
  ★**選択範囲で何も選んでいない（ページアイテムも文字のカーソルも無い）ときは断る** ――
  `Search: is Selection, but nothing is selected. Select page items or click in a text frame, or set Search: to Document in Edit > Find/Change.`
  （本体の「次を検索」もそのときは何も歩かず、設定が選択範囲のままだと本体のオブジェクトの検索は文書に向けても失敗した＝実測 2026-10-10。計画の訂正 9「文書全体を探して一言添える」を変えた。2026-10-10 にカーソルを数えるようにして文も変えた。）
  ストーリー／ストーリーの最後まで（文字だけの値）が入っていたら文書全体を探し、メッセージ欄の後ろに
  ` Search: is Story - the Object tab searches page items, so the whole document was searched.`（`To End of Story` も同じ形）を添える。
  Book Scope ON で「検索:」がすべてのドキュメント・選択範囲なら、文字と同じ文で断る（FIND-13）。（O3）
  - 訂正:

- **OBJ-04** 含める範囲と種類 ―― Object タブの設定をそのまま使う：「種類」（すべてのフレーム／テキストフレーム／グラフィックフレーム／割り当てなしのフレーム）と、
  含める範囲の5つ（親ページ・ロックされたレイヤー・非表示のレイヤー・ロックされたストーリー・脚注）。（O4）
  - 訂正:

- **OBJ-05** 歩き方 ―― 文書ごとに、本体のオブジェクトの検索に「次」を聞き続け、本体が「もう無い」と言うまで。
  同じ物が2度出たらそこで止める（1文書あたり10万回で止める守りもある）。見つかった物は、本体の歩く道の「今の物」から読む（本体の答えの入れ物は空のまま返る＝実測）。
  ★進捗バーを文書の中で進める見開きは、**見つかった物そのものに聞く**（2026-10-18・`37128e2`）＝本体の歩く道の「今の見開き」（`IObjectWalker::GetCurrentSpread`）は、「検索:」選択範囲の歩きでは文書の一覧を持たないまま読み、**InDesign の起動後の最初の物の検索が選択範囲だと InDesign が落ちた**（作者の Find Objects on Active Layer・同日に再現と直しを実測）。
  並ぶ数の上限は **1000 行**（2026-10-18 から＝GEN-34・止まった文は ` Stopped at the 1000 limit - narrow the search (Narrow Down > Pages...), or use …`。それまでは文字と同じ 300 行＝FIND-37 の文）。
  **Find Object Format が空でも探す**＝その種類のフレームが全部並ぶ（本体の「次を検索」と同じ）。読むだけで、文書を「変更あり」にしない。（O5）
  - 訂正:

- **OBJ-06** ★**一覧の検索は、利用者の選択を元に戻す** ―― 本体のオブジェクトの検索は見つけた物を選択してしまう（窓の無い文書の物は、前面の文書の「同じ番号の別の物」を選んだ＝実測）ので、
  検索の前に選択を写し、終わったら戻す（文字の検索サービスの写し）。選択とアクティブなレイヤーは検索の前と同じ（実測 M4）。
  ★**何も選んでいなかったときは、何も選んでいない状態に戻す**（2026-10-10 作者の報告「オブジェクトスタイルでの検索結果 オブジェクトを選択された状態で始まります」）＝
  写しは「何も選んでいない」を戻さない（本体が最後に見つけた物が選ばれたまま残る）ので、検索の前に選択の有無を聞いておき、無かったら写しを戻した後に選択を外す。
  （塗りの検索で外れていたのは、最後に見つけた物が別のページにあったため＝試験用ビルドの記録 OBJWALK／OBJSEL・2026-10-10。回帰＝`obj-keepsel-style-none`）
  ★**前面でない窓ありの文書の選択も戻す**（2026-10-18・Find Objects on Active Layer のクラッシュの後のコードの読み直しで見つけた）＝本体の検索は、窓のある文書なら前面でなくてもその窓で見つけた物を選ぶのに、写しは前面の文書の分だけだった（「すべてのドキュメント」で、何も選んでいない後ろの文書に、最後に見つけた物が選ばれたまま残った＝実測）。
  検索の前に、前面でない文書ごとにレイアウトの選択を読み（選んだ物／何も無し／文字や表のセル）、終わったら戻す＝選んでいた物はまた選ぶ・何も無ければ外す・文字や表のセルを選んでいたら（読み戻せないので）歩きが選んだ物を外すだけ（文字カーソルは戻らない）。実測＝3つの形とも（`work/note-scripts/2026-10-18-kfc-narrow/lc-repro.ps1` の `bg-selection`／`bg-selection-kept`）。⬜回帰の表にはまだ無い。
  ★**2026-10-19＝窓ごとに読んで、同じ窓に戻す**（コード地図の巡のブロック0の観察 O-1・O-2 を実測して当たった＝浮かせて最小化した後ろの文書の窓と、後ろの文書の2つ目の窓に、最後に見つけた物が選ばれたまま残った＝前は文書の最初のレイアウトのビューだけを読んでいて、最小化した窓はビューの一覧に出ず、2つ目の窓は読んでいなかった）＝前面のビュー（写しが戻す）を除く、開いている全部の文書の全部のレイアウトの窓（最小化した窓・分割した窓の2つ目の区画・前面の文書のほかの窓も）。実測の台本＝`work/note-scripts/2026-10-19-kfc-o6b1-o51/o1o2-probe.ps1`。回帰＝`obj-bg-sel-minimised`／`obj-bg-sel-second-window`／`obj-bg-sel-split` と各 `-armed`。
  ★**分割した窓の2つ目の区画は、それ自身の選択を持つ**（2026-10-19・KT の panesel プローブで実測＝区画ごとに選択マネージャーが別・DOM の窓の `selection` は1つ目の区画の分しか読まない）＝2つ目の区画は、本体と同じく区画のパネルの中のビューを引き直して読み、出ている間だけ数える（隠れた2つ目の区画はどの窓にもある）。同じ日の朝の直し（`1b4e6a9`）はパネルをそのまま渡していて、パネルは1つ目の区画のマネージャーを答えるので、2つ目の区画には届いていなかった。歩きが選ぶのは1つ目の区画（実測）＝2つ目の区画は、利用者がそこで選んでいた物がそのまま残る（実測＝`split-probe.ps1` の `keep-c`）。前面の文書を分割したときの2つ目の区画も写して戻す（`front-split`）。
  ★**画面の場所も戻す**（2026-10-20 夜・作者の承認「その方針でお願いします」）＝本体の検索は見つけた物へ画面をスクロールする（実測＝2ページ目を 200% で見ていて検索すると、最後の一致の3ページ目の 200% に残った＝倍率はそのまま）。検索の前に、開いている全部の文書の全部のレイアウトの窓（選択を写す窓と同じ＝分割した窓の2つ目の区画も・前面のビューも）の表示＝スプレッド・倍率・真ん中の点を控え、終わったら動いた窓だけ戻す（スプレッド→倍率→真ん中の順＝KCM の表示の同期と同じ順・同じ測り方）。**行の置換の書き込みの歩きにも使う**（選択の写しとは別＝下の⛔は選択の写しの話）＝置換の前に行の物へ寄せた画面に戻る・非表示のスプレッドの物なら画面は動かない。回帰＝`obj-hidden-spread-listed`／`obj-hidden-spread-replace-hidden`（VIEW が2ページ目のまま）。
  ⛔**置換の後には戻さない**（置換の後に写しを戻すと InDesign が落ちた＝2026-10-09）＝置換の後はその物を選んだままにする（OBJ-10 ⑨）。（O6）
  - 訂正:

- **OBJ-07** 後始末 ―― 一覧の検索・行の置換・Change All in Book・クエリの実行の後に、本体の歩く道を**前面の文書に向け直す**（前面の文書が無ければ何もしない）。
  使い終わった道が閉じた章を指したまま残り、本体の「次を検索」がそれを歩かないように。（O7）
  - 訂正:

---

## 3. 行

- **OBJ-08** ★行の形＝文書の行の下に直接（ストーリーの段は無い）――
  `[Changed |Missing ]P<ページ名>[(n)][ master][ grouped][ inline][ anchored][ hidden][ locked]  ID<UID>:<アイテム名>`
  ★**アイテム名＝レイヤーパネルに出る名前**（2026-10-10 作者の依頼「アイテム名にして欲しい・P1 ID 256: の形で、フレームの種類はなくす」「IDの後の半角スペースなくしてみてほしい」）――
  利用者が付けた名前、無ければ本体の既定の名前を `<>` に入れて（`<長方形>`・テキストフレームは書き出しの語 `<A3 words>`）、名前を付けられない物は `[]` に入れて。
  レイヤーパネルの作り方そのまま（`source/open/components/layerpanel/LayerPanelUtils.cpp` の `GetDefaultPageItemElementName`）。40文字を超えたら切って `…`。
  切り口は**前の字に付く記号**（異体字セレクタ＝IVS・濁点の結合文字 U+3099 など）の手前には来ない＝その字ごと削る側へ回す（PNL-12・ROW-16 と同じ決まり＝2026-10-13）。⬜実機は未測＝回帰 `obj-cut-mark-name`（全件の回に初めて回す）。
  （同じ日の朝に足した「ID の後ろにレイヤー名」は、この形に替えた。）
  ・ページに無い物（ペーストボード）は頭が `pasteboard`。グループの中・インライン・アンカー付きの物は、持ち主のページを名乗る（実測 M10）。
  ・★**あふれた文字の中のインライン・アンカー付きの物**（どの見開きにも置かれていない物）は `overset`（ページの名前が分かれば `P3 overset`＝文字の行のあふれと同じ形）。ペーストボードとは読まない（2026-10-10 夜・それまでの指紋の取り方＝その物のスニペットの書き出しが、この物で InDesign を落とした＝`work/kfc-crash-2026-10-10-overset-inline.xml`。OBJ-12）。（1.4.0）
  ・★**あふれた表の、表そのもののフレームは行にしない**（2026-10-20 ブロック12の再検査 NK12-5・`1613254`）――本体のオブジェクトの検索は、表があふれていると表のフレーム（`kTableFrameBoss`＝ページアイテムでない・選ぶことも見せることもできない）を「見つけた」と返す（グラフィックセルがあっても無くても・検索の形式が空＝すべてのフレームのとき。塗りなどの条件があると合わない＝実測）。それまでは `overset  [ID…:[]]` の行になり、押すと `Missing` になっていた。本体の「次を検索」もそこでは何も選ばない。あふれた表のグラフィックセルの中の物は、本体の歩きが返さない（＝行にならない・実測）。回帰 `ov-table-rows`／`ov-table-replace`。
  ・`(n)` は同じページに2つ以上あるときの順番（文字の行と同じ＝ROW-11）。親ページの物は親ページの名前と `master`（例 `PA master`）。
  ・状態の語の並び＝置き場所（master・grouped・inline・anchored）が先、届かない理由（hidden spread＝非表示のスプレッド〈2026-10-20 夜・OBJ-23〉・hidden＝非表示のレイヤーか物そのものが非表示・locked＝オブジェクト > ロックかロックされたレイヤー）が後（計画の訂正 8）。省略しない（ROW-14）。
  ・（2026-10-10 まで＝種類 `Text Frame`／`Graphic Frame`／`Frame` と、中身＝ストーリーの書き出し・配置したファイル名を出していた。名前に替えた。）
  ・並び＝文書のページ順。親ページとペーストボードの物は、その後に本体が歩いた順。（O8）
  - 訂正:

---

## 4. クリック

- **OBJ-09** ★クリック（矢印で止まったときも同じ）＝その文書を前に出し（窓の無い章には窓を付ける＝JMP-09）、物のスプレッドを表示して物の真ん中を窓の真ん中へスクロールし、
  **その物を選択する**（「検索:」が何であっても・Object タブの「次を検索」と同じ＝形 S。2026-10-09 作者の決定・試作を触った後 10-10 に確定＝JMP-29）。
  ・**ツールは変えない**（本体の Object タブの「次を検索」も、文字ツールのまま物を選択する＝実測 M5・M11b）。⚠文字の行のクリックは文字ツールに替える（JMP-29）＝本体の「次を検索」の、タブごとの動きに合わせた。
  ・**ロック・非表示の物は選択しない** ―― 何も選択されていない状態にして（画面はその物へ動かす）、メッセージ欄に `That object is locked - not selected.`／`That object is hidden - not selected.`。
  ・物がもう無ければ、文書は前に出し、行の頭に `Missing`、メッセージ欄に `Not found - the object is no longer in the document. Search again.`
  ・★**章を保存せずに閉じて開き直した後**（ブックの行は章を閉じても残る）、保存していなかった物の行は、開き直したその時に「もう無い」に決まる（2026-10-20 ブロック12の再検査 NK12-6）――InDesign は保存していなかった物の UID を、開き直した後に最初に作る物へ渡す（実測＝262 が次の新しい長方形へ）。それまでは、後から同じ UID をもらった**別の物**をクリックで選び、特殊操作（削除など＝指紋を見ない）はその物を書き換えた（実測＝削除で別の物が消えた。置換は指紋で断っていた）。今はクリック・置換・Create Outlines・特殊操作のどれも、その行を `Missing` として断る（特殊操作のメニューは灰色）。文書の検索では、閉じた文書の行は消える＝この筋は無い（実測）。回帰 `obj-book-uid-reused`／`obj-book-uid-reused-delete`。
  ・**あふれた文字の中の物**（`overset` の行＝OBJ-08）は何も選ばず、その文字があふれ出るフレームの「＋」へ画面を動かし（文字の行のあふれと同じ＝JMP-11）、メッセージ欄に `That object is in overset text - not selected.`（2026-10-10 夜）。
  ・★**非表示のスプレッドの物**は何も動かさない（2026-10-20 作者の決定＝JMP-34）＝文書を前に出さず・画面を動かさず・選択も変えず、メッセージ欄に `That object is on a hidden spread - not shown, not selected.`。行の置換（OBJ-10）の後も、その物は選ばない（何も選ばれていない状態）。回帰＝`obj-click-hidden-spread`（検索の後にスプレッドを隠して押す＝その頃は下の⬜のため、隠すのは検索の後だった）。行には `hidden spread` と出る（隠してから探したとき＝OBJ-23）。
  ・枠の印は無い（形 S＝選択が場所を示す。設計書 O9 の「外形に枠の印」は形 M のもので、作らなかった）。
  ・キーボードは一覧に残る（矢印で歩ける・Return で置換）。クリックのたびにパネルの口で取り直す（JMP-33＝「検索と置換」のタイトルバーを押した後でも、↓ が物を動かさない）。クリックの後に本体が「検索:」を変えたら戻す（JMP-32）。
  ・ダブルクリックは2回のクリック（2026-10-10 作者の決定「ダブルクリック自体の判定をなくしましょう」＝計画の訂正 10。設計書 O9 のダブルクリックの選択は作らなかった）。（O9）
  - 訂正:

---

## 5. 行の置換

- **OBJ-10** ★行の置換（右クリックの `Replace`・Return・Shift+Return）＝その物にだけ、本体の「オブジェクト形式を置換」を当てる。手順と断る順 ――
  （窓を出す前に聞く＝断るときに窓が開かない・計画の訂正 5）
  ①検索のときと設定が同じか＝タブ・種類・含める範囲の5つ・Find Object Format の条件の数・検索のオブジェクトスタイル（置換側は見ない＝文字と同じ・計画の訂正 1）。違えば文字と同じく**結果を消して**断る。
  ②置換するものがあるか（Change Object Format か置換のオブジェクトスタイル）＝`Replace: nothing to change to - set Change Object Format or an object style in Edit > Find/Change.`
  ③物がまだあるか＝無ければ行を `Missing` にして `Replace: the object is no longer in the document - search again.`
  ③b どこかの見開きに置かれているか＝あふれた文字の中なら `Replace: the object is in overset text - left as it is.`（見せることも選ぶこともできないため・2026-10-10 夜）
  ④ロックされていないか（オブジェクト > ロック・ロックされたレイヤー）＝`Replace: the object is locked now (a locked layer, or Object > Lock) - left as it is.`
  ⑤**指紋**が検索のとき（か KFC の置換が最後に取り直した後＝その行の置換か、それが動かした別の行の置換・OBJ-12b）と同じか（OBJ-12）＝`Replace: the object has changed since the search (moved, restyled or edited) - search again.`
  ⑥その章を前に出し（窓の無い章には窓）、物を表示する
  ⑦本体のオブジェクトの検索を文書の最初から**その物に着くまで**進める（着かなければ `Replace: the object no longer matches the Find/Change settings - search again.`）
  ⑧その物だけを置換（取り消し1段・段の名前は文字の行の置換と同じ）
  ⑨**その物を選んだままにする**（クリックと同じ選び方。物がもう無ければ何も選ばない。2026-10-10 作者「オブジェクトを置換した後、選択が外れるが 選択はされていて欲しい」＝10-09 の「選択を外しておきましょう」を変えた。回帰＝`obj-replace-row`／`obj-replace-two`）
  ⑩行を `Changed` にして指紋を取り直す（書き込みが動かした別の行の指紋も＝OBJ-12b）。メッセージ欄に `Replaced ID:<UID>.`（文字の REP-22 の形）
  ⑪後始末（OBJ-07）。
  置換済み（`Changed`）の行・ロックの行は、文字の行と同じく置換できない（`Replace` が灰色・Return は書かない）。Shift+Return は置換して次の行へ（REP-32）。
  ブックの章どうしで UID が同じ物があっても、その行の章の物だけを書く（前面の文書の同じ番号の物は触らない＝回帰 `obj-book-same-uid`）。（O10）
  - 訂正:

- **OBJ-11** Ctrl+Z／Redo に一覧が付いてくる ―― 物の置換を取り消すと行が置換の前（`Changed` が外れ、覚えている指紋も前の値）に戻り、やり直すと `Changed` に戻る。
  見分けは物の指紋の「前」と「後」（文字のストーリーの版＝UNDO-07 にあたる）。後から別の編集をしていても、その編集の取り消しでは行は動かない（回帰 `obj-edit-undo`）。
  ⚠オブジェクトのクエリだけのクエリの実行は、取り消しても実行の前の一覧が戻らない（パネルは空のまま・計画の訂正 2）。文字のクエリが混じった実行は、今どおり戻る。（O11）
  - 訂正:

- **OBJ-12** ★指紋＝**その物自身の保存データ**（データベースがその物を書くときと同じバイト列＝`IPMPersist::SaveAll` をメモリへ）のハッシュ（FNV-1a 64bit）と長さ。
  中に持つもの（グループの中の物・グラフィックフレームの画像）も同じように足し、テキストフレーム（とパス上の文字）は**ストーリーの版**（`ITextModel::GetChangeCount`）を足す。
  インライン・アンカー付きの物は、アンカー付きオブジェクトの設定（位置・基準点・オフセットなど）を値で足す。
  入れないもの＝テキストフレームの段（組版の結果＝どの文字がそこに流れたか）とアンカーそのもののバイト列（組版した場所を持つ）＝打つ・流れ直すで変わるが、物は変わっていないため。
  位置・大きさ・回転・塗り・線・効果・オブジェクトスタイル・中の文字・再リンクで変わり、組版のやり直し・拡大率・選択では変わらず、Undo で前の値にちょうど戻る（実測＝`work/note-scripts/2026-10-10-kfc-saveall-probe/`）。
  検索のときに物ごとに取り、置換の前に取り直して比べ（作者の決定「B」＝少しでも変わっていたら断る）、置換の後に取り直す。（O12）
  ★2026-10-10 夜から（`aa39d58`）。それまでは**その物だけのスニペットをメモリに書き出した中身**（最後の XMP の包みの手前まで）だったが、あふれた文字の中のインラインで InDesign が落ちた（スニペットの書き出しの中＝INXCORE）。今の取り方はスニペットと同じ答え（移動・濃度・入力・再リンク・組版・拡大・選択）で 20〜100倍速く、文書には何も書かない（`.indd` も「変更あり」の印も元のまま）。（1.4.0）
  - 訂正:

- **OBJ-12b** ★**KFC 自身の置換が動かした別の行の指紋も、書いた後の値に取り直す**（F2・2026-10-12 作者「直すで」）――
  指紋は物の外のことも持つ（グループは中の物・テキストフレームとパス上の文字はストーリーの版）ので、それまでは、ある行の置換が別の行の指紋を動かし、その行の置換が「検索の後で変わった」と断られていた（インラインを置換した後の、それを含むテキストフレームの行／中の物を置換した後の、そのグループの行）。
  届く行＝書く物を持つ物（グループなど）／書く物が持つ物（グループの中の物など）／書く物が見せるか置かれているストーリーを見せるテキストフレームとパス上の文字（インライン・アンカー付きの物は、置かれている文字のストーリー。表のグラフィックセルの物は、その表のあるストーリー）。
  そのうち**書く前に、行が覚えている指紋のままだった行だけ**を取り直す。書く前に変わっていた行（KFC 以外の編集）は、今どおり断る（OBJ-12 の作者の決定「B」）。
  取り直した指紋は控えるので、Ctrl+Z で物と一緒に書く前の値へ戻り、Redo で書いた後の値に戻る（OBJ-11）。1行の置換（OBJ-10 ⑩）も、まとめての置換（OBJ-18）も同じ。
  回帰＝`obj-f2-inline`・`obj-f2-thread`・`obj-f2-cell`・`obj-f2-undo-frame`・`obj-f2-redo-frame`・`obj-f2-edited-refused`（直す前の形は、試験用ビルドの故障スイッチ `object-prints-stale`・`object-prints-unbacked`・`object-prints-blind` で `-armed` のケースが踏む）・`obj-f2-rows-together`（まとめての置換の後。本物のクリックが要る）。（1.4.0）
  - 訂正:

---

## 6. Change All in Book とクエリの実行

- **OBJ-13** Change All in Book (No List) を Object タブでも使える（入口・灰色の条件・章を全部開いて抱える・取り消し1段・書いた章に窓・0の章は返す・`By chapter:` は文字と同じ＝REP-33）。
  章ごとに本体のオブジェクトの「すべてを置換」。
  ・**Find Object Format（と検索のオブジェクトスタイル）が空なら灰色**（ブックの全部のフレームを書き換えてしまうため）。スクリプトなどから押されたら
  `Change All: nothing to find on the Object tab - set Find Object Format or an object style in Edit > Find/Change first.`
  ・Change Object Format（と置換のオブジェクトスタイル）が空なら `Change All: nothing to change to on the Object tab - set Change Object Format or an object style first.`（灰色にはしない）
  ・結果の文＝`N replaced in M chapter(s).`。本体が「一部だけ置き換えた」と数えた物があれば ` (K partially)` を `.` の前に添える（N は全部＋一部＝計画の訂正 7）。選んだ文書のときの形にも同じく添える。
  ・Colour タブでスクリプトなどから押されたら `Change All: the Colour tab is not supported.`（O13）
  - 訂正:

- **OBJ-14** クエリの実行（Run Saved Queries...）――
  ・左の一覧に **Object のクエリも並ぶ**（利用者の `Find-Change Queries\Object` と、本体に付いてくる2つ＝`Drop Shadow`・`Overprint Black Stroke`）。Colour は並ばない（QRY-08）。種類の並び＝Text → GREP → Glyph → Object → Transliterate（QRY-09）。
  ・Object のクエリは本体の読み込みで入れ（タブも Object になる）、文書ごとに本体のオブジェクトの「すべてを置換」で書く（全体で取り消し1段は今と同じ）。
  ・飛ばすもの ―― Find Object Format が空＝` Skipped 1 query: nothing to find (<名前>).`／
  実行の範囲が文書の一部（ストーリー・ストーリーの最後まで・選択範囲）＝` Skipped 1 query: an Object query runs on whole documents (<名前>).`（物の検索にはストーリーが無く、文字の選択範囲はページアイテムを名指ししないため・計画の訂正 3）／
  Change Object Format が空＝` Skipped 1 query: nothing to change to (<名前>).`
  ・並びのファイル（Save Order...）の種類は `Object`。読み込みでファイルの場所が変わっていても、種類と名前で探し直す（QRY-15・QRY-16 と同じ。回帰 `obj-qd-load-by-name`）。
  ・左の行のダブルクリックで本体の「検索と置換」に読み込む（QRY-27）のも、Object のクエリで効く（回帰 `obj-qd-load`）。（O14）
  - 訂正:

---

## 7. 版

- **OBJ-15** 版は **1.4.0**（`kKFCVersion`・2026-10-10）。1.3.0＝タグ `v1.3.0`（`ff0d1b5`）・2026-10-09 に出品。（O15）
  - 訂正:

---

## 8. 文書の中の Cancel・まとめて選ぶ・まとめて置換する（2026-10-10〜11）

- **OBJ-16** ★**一覧の検索の Cancel は、1つの文書の中でも効く**（2026-10-10・ヘッダーの読み直し 101 の提案1を作者が採った＝それまでは文書の境目でしか聞かず、1つの文書の検索は最後まで止まらなかった〈上限300行で約6〜22秒＋歩き＝提案の見積もり〉）――
  進捗バーは文書ごとの区画を、見つけた物ごとに1つ進め、物がある見開きに来たらその見開きの位置まで進める。Cancel は**見つけた物ごと**に聞く。
  押されたら、その文書の途中でも止め、文書の間で止めたときと同じく `Search cancelled.`（一覧は捨てる・KFC が開いた章は返す）。回帰＝`ms-cases.tsv`（試験用ビルドの故障スイッチ `objsearch-cancel`＝n 個目で押したことにする）。（1.4.0）
  - 訂正:

- **OBJ-17** ★**物の行もまとめて選べる**（ROW-34〜38・2026-10-10 作者の決定＝物の行から始めた。設計書 O17 とコードの注は、物の行を先にした理由を「本体の整列・グループ・変形・オブジェクトスタイルを、選んだ物にまとめて当てられるように」と書く）――
  **どの見開き・どの文書の物の行でも**一緒に選べる（2026-10-10 夜＝それまでは1つの見開きの行だけ）。あふれた文字の中の物の行も選べる。
  **ページで選ばれるのは最後に選んだ行の物だけ**（ROW-36＝InDesign は1つの見開きの物しか一度に選べず、行は文書をまたぐため・作者の決定）。
  - 訂正:

- **OBJ-18** ★**まとめて選んだ物の行は、まとめて置換する**（REP-36）――
  文書ごとに本体のオブジェクトの検索で1回歩き、選んだ物に着くたびにその物だけを置換する（本体の「置換して次を検索」を物ごとに）。
  断る行（ロック・あふれの中・検索の後で変わった〈指紋〉・もう無い・歩きが着かなかった・本体が置き換えなかった）は書かずに理由ごとに数え、ほかは書く。
  全部で**取り消しの1段**（文書をまたいでも1段＝REP-36）。最後に選んだ行の文書を前に出し、その物を選んだままにする（OBJ-10 ⑨）。
  ★**書いた後に、最後に選んだ行の文書をもう一度前に出す**（2026-10-11 夜の再検査の K-2）＝閉じていた章に書くと、その章に窓が付いて前に出るため（それまでは別の章の窓が前に残り、何も選ばれていなかった）。前に出し直すときは拡大率を引き継がない（その窓の拡大率のまま）。回帰＝`sel-book-objects-front`。
  ★**Hide Previous Chapter の掃除はしない**（JMP-26c・K-3）＝掃除の close は予約で、書き込みの後に走るので、これから書く章を書き込みごと保存せずに閉じた。回帰＝`sel-book-objects-open-hide`。
  文＝`Replaced 3 objects: ID:259, ID:260, ID:261.`／`Replaced 3 of 5 objects: ... Left as they were: 1 locked, 1 changed since the search.`／`Replaced 3 objects in 2 documents: a.indd ID:259, ID:260; b.indd ID:12.`。
  選んだ行のどれも確かめ（OBJ-10 の③〜⑤）を通らなければ、窓を出す前に `Replace: none of the 5 selected rows can be replaced - 2 locked, 3 changed since the search.`（理由ごとの数）。
  歩いて1つも書けなければ `Replace: nothing was changed - <数えた理由>.`。本体がエラーで止まったら段ごと戻す（`Replace: InDesign's replace stopped with an error - nothing was changed.`・歩きのエラーなら `... search stopped ...`）。（1.4.0）
  - 訂正:

---

## 9. 物の行の特殊操作（2026-10-17）

- **OBJ-19** ★**物の行の `Special Operations ▸ Create Outlines`**（2026-10-17 作者の依頼「オブジェクト検査の検索結果の行に、特殊操作のサブメニューを追加したい／そこのなかに、アウトライン化を、選択された行のオブジェクトに対しておこなう／そうすることによってスプレッドをまたいだオブジェクトにたいして操作をおこなえる」・設計書 `docs/superpowers/specs/2026-10-17-kfc-object-outlines-design.md`＝作者の不在中に私のおすすめで決めた形（D1〜D11）＝**同じ夜に作者が承認**（「それでおねがいします」））――
  物の行の右クリックのメニューは文字の行と別（ROW-32）＝`Replace` と `Special Operations ▸ Create Outlines`。右クリックした行が、まとめて選んだ行の1つなら選んだ行すべて、そうでなければその1行（Replace と同じ＝REP-36）。
  本体の「書式＞アウトラインを作成」と同じく**元の枠を消してアウトラインに置き換える**（`ICreateOutlinesUtils::CreateCOFrameCmd`・枠ごとに1つ）。**スプレッドをまたいでも、文書（ブックの章）をまたいでも1回で**（本体のは選択＝1つのスプレッドの中だけ）。
  書かない行（理由ごとに数える）＝文字の枠でない（本体の振り分け `FilterUIDList`）・ロック・あふれの中・もう無い（Missing）・もうアウトラインにした・文書が見つからない／開けない。**検索の後で物が変わった（指紋）では断らない**（Replace と違う＝アウトラインは検索の条件に関わらず、行が名指す物にかける）。
  全部で**取り消しの1段**（Edit メニューの名前は `Create Outlines`・文書をまたいでも1段）＝パネルも Ctrl+Z／Redo に付いていく（`Undo: Create Outlines - the rows are back as they were before it.`＝UNDO-02）。KFC が開いた章（閉じていたブックの章）に書いたら窓を出す（保存できるように＝Replace と同じ）。
  書いた行は頭に `Outlined`・目玉が付く（ROW-45）・`Replace` は灰色（右クリックに出ない）・行の物はできたアウトライン（クリックでそれを選ぶ）。行の言葉（`[ID278:<alpha one>]`）は検索した時の物のまま。
  文＝`Outlined 2 of 3 objects: ID:278, ID:301. Left as they were: 1 not a text frame.`／`Outlined 1 object: ID:301.`／`Outlined 4 objects in 2 documents: rtoC.indd ID:278, ID:301; rtoD.indd ID:278, ID:301.`／書ける行が無い＝`Create Outlines: none of the 3 selected rows can be outlined - 3 not a text frame.`（1行なら `Create Outlines: <理由の文>`）。
  ✅ 実機（2026-10-17・テスト用ビルド・KIDMCP）＝2つのスプレッドの文字の枠2つと画像の枠1つを Select All Rows で選び、右クリック→`Special Operations`→`Create Outlines`：2つのスプレッドとも文字の枠が消えてアウトライン（ポリゴン）になり、画像の枠は `not a text frame` で残る・行は `Outlined` と目玉・押すと2ページ目のアウトラインが選ばれる・Ctrl+Z で2つの枠と行が戻り未保存の印も元どおり・Redo でまたアウトライン／選んでいない1行の右クリックはその行だけ／閉じていたブックの2つの章を Select All Rows で→2つの文書に1段で書き、章に窓が出る・Ctrl+Z 1回で両方戻る／文字の行の右クリックは `Replace` だけ。⬜まだ＝鍵の掛かった物・あふれの中の物・アンカー付き／インラインの枠・連結した枠・全件の回。
  ★**灰色＝アウトラインにできる行が1つも無いとき**（2026-10-17 夜の作者「CreateOutline とかの新しく作るの、実行できないときは色を変えられるとうれしい」）＝物の行・まだアウトラインにしていない・文書が開いていれば物があり、スプレッドの上・鍵なし・本体の振り分けが文字の枠と言う、を読むだけで問う（文書を開かない）。閉じた章の行は押せる（押すと章を開いて、書けない理由を言う）。✅ 実機＝画像の枠の行で灰色・文字の枠の行で押せる。
  - 訂正:

- **OBJ-20** ★**物の行のほかの特殊操作**（2026-10-17 夜・作者が候補から選んだもの＝「Fitting　やりましょう」「Move to Layer：今レイヤーパネルで選ばれているレイヤーに移動かな」「Lock／Hide／Arrange／拡大縮小を100%に再定義／スタイルの上書き消去　やりましょう／UnLockもできるかな？」「Delete　やりましょう」・設計書 `docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md`＝作者の不在中に私のおすすめで決めた形（E1〜E15）＝⬜作者の確認待ち）――
  `Special Operations ▸` の中は ⚠2026-10-18 午後から abc 順・区切り線なし（作者の依頼＝ROW-32）。それまでは本体の Object メニューの並び：`Create Outlines`／─／`Fitting ▸`（Fill Frame Proportionally・Fit Content Proportionally・Content-Aware Fit・Fit Frame to Content・Fit Content to Frame・Center Content）／`Arrange ▸`（Bring to Front・Bring Forward・Send Backward・Send to Back）／`Transform ▸`（Rotate 90 Degrees Clockwise・Counter-clockwise・Rotate 180 Degrees・Flip Horizontal・Flip Vertical／─／Redefine Scaling as 100%・Clear Transformations）／─／`Lock`・`Unlock`・`Hide`・`Show`／─／`Move to Active Layer`・`Clear Object Style Overrides`／─／`Delete`。Show・90°／180°・反転・Clear Transformations は私が対として足した。名前は本体の英語のコマンド名（`.fr` は ASCII の約束＝`°` の代わりに `Degrees`）。サブメニューの名前は KFC の文字列キー（素の「Fitting」は本体のキー＝日本語の本体では日本語で出た・2026-10-17 実測）。
  どの行に＝Create Outlines と同じ（右クリックした行がまとめて選んだ行の1つなら選んだ行すべて・REP-36）。**スプレッドをまたいでも、文書（ブックの章）をまたいでも1回で**、全部で**取り消しの1段**（Edit メニューの名前は操作の英語名＝`Undo Fit Content Proportionally` など）。KFC が開いた章に書いたら窓を出す（Replace と同じ）。
  書き方＝本体の Facade（`IFrameContentFacade`・`IArrangeFacade`・`ITransformFacade`・`IPageItemLockFacade`・`IPageItemVisibilityFacade`・`ILayerFacade::MoveToLayer`・`IObjectStylesFacade::ApplyObjectStyleToUIDList`・`IPageItemScrapFacade`）を**スプレッドごとに1回**（本体のコマンドが受ける選択の単位＝2つのスプレッドの物を1回で渡すと MoveToLayer が2つ目のスプレッドの物を1つ目へ動かした・2026-10-17 実測で直した）。回転・反転は物ごとに、その物の中心で（スプレッドをまたぐ選択の中心は意味が無い）。★**中心と座標は本体の変形と同じ読み方**（2026-10-23 ブロック20 の監査・本体と並べて実測）＝中心は変形パネルの「線幅を含める」が入なら線の囲み・切ならパスの囲みの中心（本体の 90°回転も同じ＝それまでの KFC は線の囲みに決め打ち）／`Clear Transformations` はパネルの「変形値の合計」が切なら、グループの中の物をグループの座標で消す（本体の「変形を消去」も同じ＝入なら貼り込み板で消す）。Move・反転・シアーは本体もこの設定に関わらず貼り込み板の向き（KFC も同じ）。
  断る行（理由ごとに数える）＝物の行でない・文書が見つからない／開けない・もう無い（行は Missing）・あふれの中・**鍵の掛かったレイヤーの物**（全部の操作）・**Object > Lock の物**（Unlock 以外）・**本体の `Can…` が「できない」**（例＝`no content to fit`・`in front already`・`at the back already`・`at 100% already`・`not transformed`・`cannot be locked`）・Unlock＝`not locked`・Hide＝`hidden already`・Show＝`not hidden`・Move to Active Layer＝`in a group or in text`（本体も一番外の物だけ）・`no layer named "…"`（その文書に同じ名前のレイヤーが無い＝作らない）・`on "…" already`・Clear Object Style Overrides＝`no object style`・変形（値の要る4つ・90°・180°・反転）で、物の囲みが動いた後にスプレッドのペーストボードからペーストボード1つ分より外へ出る物＝`beyond the pasteboard`（書く前に測って断る＝本体の窓の「ペーストボードから失われる」「大きくなりすぎる」の警告と、ドラッグをペーストボードの中に留める本体の形に合わせた・本体の拡大・縮小は 999999999% の警告を通すと落ちた・2026-10-23 D20-1）。**検索の後で物が変わった（指紋）では断らない**（OBJ-19 と同じ）。
  ★**変わった物だけに印**＝書いた後に物のデータ（指紋）が動かなかった物（Arrange は重なりの位置が動かなかった物）は `unchanged` と数え、行は変えない。⚠本体の Fit Content Proportionally・Fill Frame Proportionally は、もう合っている枠でも画像のデータを書き直す＝「1 object」と数える（Center Content・Fit Frame to Content は `unchanged`）。
  書いた行＝頭に操作の語（`Fitted`・`Arranged`・`Rotated`・`Flipped`・`Redefined`・`Reset`・`Locked`・`Unlocked`・`Hidden`・`Shown`・`Moved`・`Cleared`・`Deleted`）・**目玉が付く**（ROW-45）・行の場所（ページ・`locked`・`hidden`・`grouped` など）は**書いた後の物で読み直す**（ページが変わったら同じページの中の番号は消す）・指紋も書いた後のもの＝その後の `Replace` がそのまま通る。**`Replace` は灰色にしない**（物は同じ物のまま）＝`Delete` だけ灰色（物が無い・行は `(R/N)` の R に数える）。Replace・Create Outlines を後からかけると、行の語はそれ（`Changed`・`Outlined`）になる。探し直しで同じ物が見つかったら、見つけた行が語と目玉を受け継ぐ。
  Move to Active Layer の対象＝**前面の文書の Layers パネルで選ばれているレイヤー**（`ILayerUIUtils::QueryContextActiveLayer`＝本体の Layers パネルのメニューが対象を取る口）の**名前**。ほかの文書・章は同じ名前のレイヤーへ。前面に文書が無いときは灰色。
  文＝`Fit Content Proportionally: 2 of 8 objects - ID:266, ID:302. Left as they were: 6 no content to fit.`／`Move to Active Layer: 5 of 8 objects to "Images" - ID:266, .... Left as they were: 2 in a group or in text, 1 on "Images" already.`／`Lock: 9 of 11 objects in 2 documents - kfc-sp-a.indd ID:266, ...; kfc-sp-b.indd ID:278, .... Left as they were: 2 cannot be locked.`／書ける行が無い＝`<操作名>: none of the 3 selected rows can be changed - ...`・1行なら `<操作名>: <理由の文>`・書いたが何も変わらない＝`<操作名>: nothing was changed - 1 unchanged.`。取り消し＝`Undo: Send to Back - the rows are back as they were before it.`（UNDO-02）。
  ★**灰色＝選んだ行のどれもできないとき**（SET-11＝灰色でも項目は出す）＝上の扉と本体の `Can…` を読むだけで問う（文書を開かない・閉じた章の行は押せる）。例＝もう一番前の物の行で Bring to Front・Bring Forward が灰色、鍵の掛かった物の行で Unlock だけ押せる。
  ✅ 実機（2026-10-17 夜・テスト用ビルド・KIDMCP）＝2つのスプレッドの画像の枠に Fit Content Proportionally を一度に（ほかの6行は `no content to fit`）・Fitting の6つ・Send to Back（Ctrl+Z／Redo＝重なりの位置で追う）・90°回転（中心で・DOM で -90°）・左右反転・Redefine Scaling as 100%（150%→100%）・Clear Transformations・Lock／Unlock・Hide／Show・Clear Object Style Overrides（塗りと線の上書き→スタイルの値）・Delete（Ctrl+Z で物も行も戻る・Redo）・Move to Active Layer（直した後＝2つのスプレッドの物がそれぞれのページのまま・グループの中2つと既に Images の1つを数えて断る・レイヤーの無い文書の行は断る）・スプレッドをまたいだ Send to Back と Delete・閉じた2章のブックを Select All Rows で Lock（2文書1段・章に窓・どちらかの Ctrl+Z で両方戻り行も戻る）・鍵の掛かった物の行のメニュー。
  ★**値を聞く変形（段2・2026-10-17 夜）**＝`Transform ▸` の頭に `Move...`・`Scale...`・`Rotate...`・`Shear...`（─ の上）。押すと**本体のダイアログと同じ形の窓**（作者「公式の移動や回転拡大のダイアログを写真にとって、KFCのをそれに似せてほしい」＝写真は `work/note-scripts/2026-10-17-kfc-object-special/official-*.png`）＝左に欄・右に OK／Cancel を縦に・値は単位つき（`0 mm`・`100%`・`0°`）で、確定すると単位つきに直す。Move＝`Position` の枠に Horizontal／Vertical、線の下に Distance／Angle（片方から他方を計算・上が正の角度）・数字だけなら前面の文書の定規の単位・pt mm cm in pc px Q H を打てる・右と下が正・全部の物を同じ距離。Scale＝Scale X／Y（%）と鎖（押すと縦横が一つ・既定は押した状態）＝★鎖の絵は本体の鎖と同じ（2026-10-17 夜の作者「チェーンの様なアイコン、公式と同じにできないかな？」「鎖のマークは、押すと変りますね。アイコンも動作も」＝本体の Scale を4つのテーマと、静止・ポインターの上・キーボードのフォーカス×つないだ・切れたで撮って作った形と色・実機で本体と1階調以内＝`docs/ai-notes/kfc-layer-chain-cleartype-2026-10-17.md` §3）。Rotate＝Angle（反時計回りが正）。Shear＝Shear Angle（-90〜90）と `Axis`（Horizontal／Vertical）＝**垂直の軸は本体のシアーダイアログ（軸：垂直）と同じ行列**（実測＝[0.866,-0.5,0,1.1547]・回転30 シアー30）。どれも**各物はその物の中心で**（本体のダイアログは基準点）。本体の Copy と Preview は無い。前に打った値はセッションの間だけ覚える（距離は単位ごと）。Cancel＝何もしない。値が何も変えない（Move 0/0・Scale 100/100・角度 0）＝書かずに `<操作名>: the values change nothing - no object was written.`。KFC 独自の決まりの説明は欄のヒント（マウスを置くと出る）。★**有限でない値は窓が断る**（2026-10-23 D20-1＝400桁の数字は窓の JS で `Infinity` になり、それまでは素通りして変形に渡った）＝Move・Scale・Rotate の OK はそれぞれの警告（`Type a distance - …`・`Type a percentage other than 0.`・`Type an angle in degrees.`）で窓に留まる（Shear はもとから -90〜90 で断る）・念のため C++ でも `<操作名>: the values are too large - nothing was changed.`。行の語＝`Moved`・`Scaled`・`Rotated`・`Sheared`。取り消し1段。窓は KFC のスクリプトの場（`KFC`）で出す ScriptUI。
  ★**Arrange をまとめて選んだ行に**＝本体の複数選択と同じく、**スプレッドごとの物をまとめて1回**で聞いて送る（物どうしの前後の順は保つ）＝1つずつ送ると一番後ろの物が外れて順が変わった（2026-10-17 夜・作者「複数を　後ろにおくったりして、うまくいくの？順番におくる？」で見つけて直した・実機で4つとも一番後ろ→`nothing was changed - 4 at the back already`、前の2つは順を保った）。
  ⛔**Run Script（選んだ行の物にスクリプトをかける）＝見送り**（2026-10-17 夜・作者「では、見送りましょうScriptは」・設計書 E13）＝再提案しない。
  - 訂正: ★2026-10-23（作者の判断 J20-1「おすすめで」）＝値を聞く窓（Move...・Scale...・Rotate...・Shear...・Narrow Down の Pages...＝ROW-46）を**本体の部品の窓**に作り直した（設計書 `docs/superpowers/specs/2026-10-23-kfc-native-value-dialogs-design.md`・`ui/KFCSpecialDialogs.cpp`・`KFCUI.fr`）＝欄は本体の測る欄・角度の欄・％の欄＝**前面の文書の定規の単位で読み書きし、本体が読む単位・書き方は全部読む**（Ciceros も、パイカの `1p6` も＝実測 `1p6`→`6.35 mm`）／範囲の外は**本体の言葉で断って窓に留まる**（実測 `値は -35277.778 mm から 35277.778 mm までの範囲内でなければなりません。`＝欄の範囲は ±100000 pt・有限でない値は打てない）・Scale の 0% とページの空は KFC の文で断る（前と同じ）／鎖は**本体の鎖のボタンそのもの**（押すと絵と状態が自分で切り替わる＝撮った絵から作った `kChain` は消した）／Move は確定した組から他方を計算し、OK は**最後に打った組**で答える（確定せずに Enter を押しても打った組＝実測）／Scale は鎖がつながっている間、1打ごとにもう片方へ写す（前と同じ）／前の値はセッションの間（距離は pt で持ち、開いたときの定規の単位で出す）／Copy と Preview は無い（前と同じ）／**欄のヒントは無くした**（本体の窓に説明は無い）＝「各物はその物の中心で」はこの地図の決まりのまま。OK で増える取り消しの段は KFC の操作の1段だけ（実測＝`[0] Move`・窓の空の段は無い・Cancel は0）。✅ 実機（2026-10-23・テスト用ビルドと出荷ビルド・KIDMCP の本物のキー＝`work/note-scripts/2026-10-23-kfc-decisions/live_j20_1.ps1`）＝移動3通り・Scale の鎖つき／外し／0%・範囲の外・Rotate・垂直のシアー（本体と同じ行列）・前の値・Cancel・Pages の全項目、回帰＝objsp 12・非表示のスプレッド 4・Narrow Down 9 PASS（悪化0）。

- **OBJ-21** ★**Find Objects on Active Layer**【2026-10-18 午後に **Narrow Down ▸ On Active Layer**（ROW-46）へ移った＝パネルメニューから外し、文字の段にも＝下の訂正】（パネルメニュー・2026-10-17 夜・作者の S3「同じレイヤー」＝設計書 `docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md` E14・作者「Task 7 も進めてください」）――
  パネルメニューの Find の下（SET-01）。**Object タブの検索**を Find と同じ扉で回し（前の一覧は段になる＝ROW-39）、行を**前面の文書の Layers パネルで選ばれているレイヤー**（Move to Active Layer と同じ読み方＝`ILayerUIUtils::QueryContextActiveLayer`）の**物だけ**に絞る。All Documents・ブックのほかの文書は**同じ名前のレイヤー**の物だけ。そのレイヤーが無い文書は探さず、名前を挙げる＝`6 hit(s) on layer "Images" in 1 of 2 document(s).  1 chapter(s) could not be searched (kfc-layer-b.indd: no layer named "Images").`。
  照合は本体＝ふつうの物の検索と同じ（Find Object Format が空なら、Type の枠が全部＝実測）。絞り方＝本体の物の検索の結果を、歩く途中で物の文書のレイヤーで落とす。
  一覧は**レイヤーを覚える**＝段の見出し `[Object/Document/Layer: Images]`（ROW-39・描くだけで名前には入れない）・状況行 `4 hit(s) on layer "Images". …`（0件は `No matches on layer "Images"`）。**Search These Results Again**（`Searched these results again: 5 hit(s) on layer "Images".`）・**Search This Document Again** も同じレイヤーで探す＝その文書にそのレイヤーがもう無ければ断る（`… the document has no layer named "Images" now - the rows are left as they were.`）。行の Replace・特殊操作・目玉はふつうの物の行と同じ。
  **灰色**＝`Find in …` の灰色の条件（SET-05）に加えて、検索と置換のタブが Object でないとき・前面に文書が無い／Layers パネルにレイヤーが無いとき。台本から ID で押されたときは文で断る（`Find Objects on Active Layer searches the Object tab - the Find/Change dialog is on the Text tab.`）。名前は英語で固定（台本が名前で引ける）。キーボードショートカットに割り当てられる（SET-13＝2026-10-18 作者が承認）。
  ✅ 実機（2026-10-17 夜・テスト用ビルド・KIDMCP）＝2つのレイヤーの文書（レイヤー 1 に3つ・Images に2つとグループの2つ）で、ふつうの Find は7行、Find Objects on Active Layer は Images の4行（グループの中の2つは `grouped`）・見出し・状況行／物を足した後の Search These Results Again（5行）・Search This Document Again（6行・状況行は `Searched this document again: 6 match(es).`）／Text タブで灰色／All Documents でレイヤーの無い2つ目の文書を名前で挙げた。回帰＝変更あり／なしの突き合わせで悪化0（`docs/ai-notes/kfc-layer-chain-cleartype-2026-10-17.md` §4）。
  - 訂正: 2026-10-18 午後＝パネルメニューの項目を外し、段の見出しの Narrow Down ▸ On Active Layer にした（作者「FindObjectのパネルメニューを[object]とかの行の絞り込みの検索の仕組みに変えたい」＋文字の段にも＝作者が私のおすすめを選んだ）。レイヤーの読み方・名前で他の文書のレイヤーを引く・無い文書を名前で挙げる、は同じ。違い＝「検索と置換」のタブに関係なく段の検索の条件で探す・見出しは `… / Layer: Images`（条件として後ろに）・重ねられる・キーボードショートカットは無い（SET-13）。

- **OBJ-22** ★**Frames of These Matches の一覧**（段の見出しの Narrow Down の最後の条件＝ROW-46・2026-10-18 作者「そのフレームの行の、オブジェクトの検索ダイアログの置換の方の条件を当てたい、それで置換したい。あとはスペシャルの動作かな」「ただその場合、検索ダイアログでオブジェクトタブにかえないとだめかな」）――
  文字の段から押すと、条件に通った一致を**その一致の枠の物の行**にした新しい段（同じ枠は1行・表のセルや脚注の中の一致は本文の枠・インラインの枠の中の一致はその枠・パス上の文字の一致はそのパス）。**あふれた文字の一致は枠が無いので行にならず**、文の後ろに ` 1 match(es) in overset text have no frame.`。上限は**枠で数える**（物の一覧の 1000＝OBJ-05）・メッセージ欄の数は `frame(s)`。文書の一部（Story・To End of Story・Selection）を探した段から絞っても同じ（2026-10-19 ブロック8の再検査 D8-5＝それまではその道だけ、上限が文字の 300・文が `hit(s)`・あふれの数を言わなかった）。行は物の行＝クリックで枠を選ぶ（OBJ の着地）・物の行の Special Operations（OBJ-19・OBJ-20）と Clear from Results（ROW-47）が使える。この一覧の上の Narrow Down は物の条件（`Pages...`・`On Pasteboard`）だけ。
  ★**Replace＝「検索と置換」の Object タブの Change Object Format（とオブジェクトスタイル）で書く**――**ダイアログのタブは切り替えなくてよい**（Text タブのままで書ける）。書く間だけ Object タブに「Find 側は空・種類は All Frames・含める範囲は一覧の検索のときのもの」を入れて書き、書き終えたら Object タブも画面のタブも元に戻す（Find 側を空にするのは、この一覧の行が物の検索で見つけたものではないため）。Object タブに置換の形が無ければ断る `nothing to change to - set Change Object Format or an object style on the Object tab of Edit > Find/Change.`。Object タブを入れられなければ書かずに断る `the Object tab could not be set for the frames (…)`、戻せなかったら書いた後に知らせる `Find/Change could not be put back as you left it (…) - check Edit > Find/Change.`。
  ✅ 実機（2026-10-18・テスト用ビルド・KIDMCP・`work/note-scripts/2026-10-18-kfc-narrow/n3.ps1`）＝F-1（連結した A→B→C の A・B、表のセルと脚注を持つ E、インラインの枠、ペーストボードの P＝5 frame(s)・一致の無い C は無い・あふれの1件の文）・F-2（Frames の後に On Pasteboard＝P だけ）・F-3（枠の行のクリックで枠が選ばれる）・F-4（1100 枠で 1000 で止まり、文が Narrow Down > Pages... を名指す）・F-5（Text タブのまま A に線 3pt を書いた・タブは Text のまま・「検索と置換」の設定の差 0・Ctrl+Z で線が戻った）。⬜まだ＝Object タブに Find 側の設定がある人の設定で Replace（入れて戻す道は F-5 で通った）・ブックの章・「検索と置換」を開いたまま（Object タブを見せているとき）。（1.4.0）
  - 訂正:

---

## 10. 非表示のスプレッドの物（2026-10-20 夜）

- **OBJ-23** ★**非表示のスプレッド（ページパネルの「スプレッドを隠す」）の物も一覧に出し、行に `hidden spread` と書く**（作者「検索する事自体が大丈夫なら、検索結果のツリーに出して欲しい　非表示スプレッドと表示をつけて」「その行を選択されると、アラートがでるかもですが、これはIndesignの仕様なのでそれでいいかも」→方針を出して「その方針でお願いします」）――
  ・本体の `SearchObject` は見つけた物を見せようとし、非表示のスプレッドの物では呼び出しの中で本体のアラート `オブジェクトを表示できません : アイテムが非表示のスプレッド上にあります。`（ボタン＝**「スプレッドを再表示」が既定**と「OK」）を出す＝検索が止まり、既定で答えるとスプレッドの非表示が解けて文書が変わる（実測）。**「OK」なら何も変わらず、その物を一致として返し、歩きは続く**（実測＝トレース `OBJWALK step=1 result=1`）。
  ・⇒非表示のスプレッドがある文書を歩くときだけ、`SearchObject`・`ReplaceObject` の1回ごとに本体のアラートを止め（`CAlert::SetShowAlerts`）、KFC のアラートの横取り（`IAlertHandler`＝SDK がプラグインにボタンを選ばせる口・UI 半分の `KFCAlertAnswer.cpp`）が、**歩きが立っている物が非表示のスプレッドにあり、アラートに OK があるときだけ OK を押す**（model の `KFCHiddenSpreadAlert`）。ほかのアラートは、止める前にアラートが出る設定だったなら画面に出す（いつもどおり）・止まっていたなら（スクリプトの実行中）既定のボタン＝今までどおり。非表示のスプレッドが無い文書では何もしない。
  ・行＝`P3 hidden spread  [ID…:<…>]`（ROW-13・OBJ-08）。文字の検索の一致の行にも付く（一致の枠のスプレッド）。検索したときの状態（hidden・locked と同じ＝後で表示に戻しても語は残る）。
  ・クリック＝何も動かさない（OBJ-09・JMP-34）＝本体のアラートも出ない（KFC が先に非表示を見分けて行の注を出す）。
  ・**行の置換＝書く**（本体の「すべてを置換」も非表示のスプレッドの物をアラートなしで置き換える＝実測）＝画面は動かさず・置換の後も選ばない（OBJ-09）。書き込みの歩きが途中の非表示の物を通るときも止まらない。本体の置換が書かなければ、メッセージ欄に `Replace: InDesign's replace would not run on this object - nothing was changed.`（いつもの断りの文）。
  ・Change All（`ReplaceAllObject`）＝今までどおり（見せる段が無い＝アラートは出ない・非表示の物も置き換える＝本体と同じ）。
  ・物の行の特殊操作（OBJ-19・OBJ-20）も書く（本体の選択を使わない＝アラートは出ない・スプレッドは隠れたまま）＝2026-10-23 実測（Lock・`work/note-scripts/2026-10-23-kfc-block20-defect/live_d20_hidden.ps1`）。
  回帰＝`obj-hidden-spread-listed`・`obj-hidden-spread-replace-hidden`・`obj-hidden-spread-replace-after`（`hs-obj4-make.jsx`）・文字は `click-hidden-spread`。
  - 訂正:

---

## 11. 物の中の文字を探す（2026-10-21）

- **OBJ-24** ★**物の中の文字を探す範囲**（Search Text in These Objects＝ROW-49 の中身・設計書 `docs/superpowers/specs/2026-10-21-kfc-text-in-object-results-design.md` の T3・T6）――
  物ごとに文字の範囲を作り、範囲ごとに**本体の「検索: 選択範囲」と同じ公式の歩き**で探す（範囲のフォーカスを `QueryFocusListWalkerScope(…, collectStoryRanges=kTrue)` で＝KFC の Search: Selection の段の探し直しと同じ道。KT の `objtext` で、その枠の文字を選んだ本体の Selection と切れ目も一致も同じと測った＝M1。作者 10-21「出来る限り公式の方法で（他の検索とかと同じようにで）不具合がないように」）。実際に文字を選んでは探さない（選択と画面が動く・窓なしの章では選べない）。一致を行にする所から先は Find と同じ1本。
  ・テキストフレーム＝その枠に組まれている文字（`IMultiColumnTextFrame::TextStart` から `TextSpan` 文字・本文の終わりの文字は除く＝公式 `SnpManipulateTextFrame::GetTextFrameTextRange` の形・枠の一覧に傷があればその枠まで組んでから＝`RecomposeThru`。`RecomposeFully` は使わない）。連結したストーリーでも、その枠の部分だけ。
  ・あふれ＝その枠がストーリーの**最後の枠**のときだけ、最後の枠の終わりから本文の終わりまで（「最後の枠か」＝枠の最後の段が枠の一覧の最後か）。
  ・表のセル・脚注＝範囲の中に錨・参照があるものは、範囲の歩きに入る。表が次の枠へ続くときは、枠ごとにその枠に描かれた行だけを集める（続きの枠にも表の錨がある＝M5）。
  ・インライン・アンカー付きの枠＝範囲の中にあるもの（表のセル・脚注の中のものも＝`ITextUtils::CollectOwnedItems` の nested）の枠の文字を、同じ決まりで足す。錨がその見つかった枠に描かれているものだけ・入れ子は深さ 8 まで。
  ・パス上の文字＝その物のパスの文字（`IMainItemTOPData` の枠）。テキストフレームにパス上の文字もあれば両方。
  ・文字の無い物（四角・画像の枠・空の枠）＝何も探さず、数をメッセージ欄で言う。物の行がグループになることは無い（本体の物の検索はグループの中の枠を行にする）。
  ・同じストーリーの範囲は、重なる・隣り合うものをつないで1つにしてから歩く（同じ一致を2回数えない・隣り合う2つの見つかった枠にまたがる一致も見つかる）。片方の枠だけなら、その端をまたぐ一致は出ない（本体の Selection と同じ）。
  ・**一致の枠で確かめる**＝一致が描かれている枠の持ち主（`KFCNarrow::FrameOwner`）が範囲を取った枠でなければ、その行を落とす（上限には数えない）。描かれる枠の無い行（あふれ）は、そのストーリーのあふれを足したときだけ残す。次の枠へ続く脚注の、次の枠の部分はこれで落ちる（回帰 `tio-footsplit-anchor`）。
  ・タブのスイッチ（親ページ・非表示のレイヤー・ロックしたレイヤー・脚注など）はそのまま効く＝外れる物の範囲は空になり、何も探さない（M6）。行には今までどおり `hidden`・`locked`・`hidden spread` が付く。
  ・**既知の限り**：脚注の参照が見つかっていない枠にあり、続きだけが見つかった枠にあると、その続きは探さない（回帰 `tio-footsplit-cont`）。★2026-10-23 作者の決定＝**今のまま**（続きも探すには、枠に描かれた脚注の部分をパーセルから拾う手が要る）。
  ・新しい段は章ごとに物の UID を覚え（`Chapter::searchedObjects`）、探し直しのたびに範囲を作り直す（今の中身を探す）。閉じて開き直した文書の、保存していなかった物の UID は手放す（次に作った物がその UID を取っても探さない＝回帰 `tio-book-uid-reused`・`tio-book-again-uid-reused`）。消えた物は探さず、数を言う。
  ・ブックの段＝開いていない章は窓なしで開き、探したら返す。All Documents の段＝開いている文書の物だけ探し、物の検索の後に閉じた文書は名前で言う。「検索: 選択範囲」で作った物の段でも使える（行が物を名指しているので、その時の選択は要らない）。
  ✅ 実機（2026-10-21・テスト用ビルド）。速さ＝物 1000 個（範囲 1000・一致 100）で押してから状態の行まで約 11.8 秒（M7）。（2.0.0）
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- ✅（2026-10-19 ブロック8の監査・同じ日に実機で確かめた）OBJ-22：パス上の文字の一致＝そのパス。それまでは一致の枠から階層を上がって最初の「枠」で止まる形で、パス上の文字では本体が描かない別の物（`kTOPSplineItemBoss`＝パスとは階層でつながっていない）の行になる見込みだった（設計書の N5「パス上の文字はそのパス」と違う・作り始めの実測 M3 の答えは設計書に無い・回帰 `nd-frames` にパス上の文字は無い）→公式の2手（`ITOPFrameData::GetMainSplineItemUID`／`ITextUtils::QuerySplineFromTextFrame`＝`SDKLayoutHelper::GetGraphicFrameRef`）に寄せた。実機＝回帰 `nd-frames-path`（行は楕円 `pathOval`）合格・`-armed`（故障スイッチ `frame-owner-climb`＝前の形）は本体の描かない物の行 `<cat path>`＝見込みどおり（どちらも合格）。
- ✅（2026-10-19 ブロック8の再検査・同じ日に実機で確かめた）OBJ-22：文書の一部を探した段から Frames of These Matches で絞った一覧＝文は `frame(s)`・あふれの一致の数・上限は物の一覧の 1000（D8-5＝その道だけ文字の一覧として扱っていた）。回帰＝`nd-frames-part`（To End of Story の段・2 frame(s)・あふれ1）／`-armed`（故障スイッチ `part-narrow-as-text`＝前の形＝`2 hit(s)`・あふれの注なし）＝どちらも合格。上限 1000 は回帰に無い（コードで読んだ）。

- ✅（2026-10-20 夜・作者の答え＝飛ばさずに一覧に出す＝OBJ-23 で直した）（以下はその時の記録）⬜（2026-10-20・作者の判断待ち＝memory `kbs-project`）**非表示のスプレッドに物があると、物の検索が本体のアラートで止まる**（不具合）＝本体の `SearchObject` は見つけた物を見せようとし、非表示のスプレッドの物で `オブジェクトを表示できません : アイテムが非表示のスプレッド上にあります。` を出す（呼び出しの中＝KFC の検索はアラートが閉じるまで止まる）。★既定のボタン（Enter）で答えると、**そのスプレッドの非表示が解け、文書が変更済みになる**（実測＝SDK 側 `work/note-scripts/2026-10-20-kfc-hidden-spread/measure_obj_alert.ps1`）。⚠アラートだけを止める（`CAlert::SetShowAlerts`）と、本体は黙って非表示を解く（実測）＝それはしない。おすすめ＝物の歩きで非表示のスプレッドを飛ばす（`IObjectWalker::SkipSpread`）＝その物は一覧に出ない（文字の検索は非表示のスプレッドの一致も出す＝JMP-34）。置換の歩き・Change All が同じかは未実測。物の検索は 1.4.0（＝2.0.0）からなので、出品済みの 1.3.0 には無い。
- ⬜（2026-10-17 夜・実機は主な道だけ）OBJ-21：試していない＝ブック（章ごとの同じ名前のレイヤー）・マスターの物・インライン／アンカー付きの物（どのレイヤーで数えるか）・非表示／ロックしたレイヤーが選ばれているとき・Search This Document Again でレイヤーが無くなった文書（断る文）・300行の上限の近く・回帰のケース（台本を段に合わせた後の全件の回に足す）。
- ⬜（2026-10-17 夜・実機は主な道だけ）OBJ-20：試していない＝All Documents で2つの文書の行をまとめて選んだ書き込み（KIDMCP の `click_widget` に修飾キーが無い＝ブックの Select All Rows で2文書を測った）・インライン／アンカー付きの物（回転・削除で本文が動く）・マスターの物・非表示のレイヤーの物・300行の上限の近く・灰色の判定の重さ（行が多いときの右クリック）・回帰のケース（全件の回に足す）→（2026-10-23 ブロック20 の無駄取り）回帰に7本＝書き込みの文を、直す前のビルドで取った答えで釘＝`objsp-lock-doc`・`objsp-outline-doc`（1 of 4・3 not a text frame）・`objsp-front-left`（Bring to Front・1 in front already, 2 unchanged）・`objsp-move-layer`（to "L2"）・`objsp-lock-nine`（8つの ID の後に , ...）・`objsp-lock-book`・`objsp-outline-book`（2つの文書の名前の後に ID）。値の要る4つと Pages... の窓は回帰の外の台本 `work/note-scripts/2026-10-23-kfc-block20-waste/live_w20_dialogs.ps1`（題・欄・前に打った値・状況行・同じ回の直す前と後で一致）。値の要る4つ（段2）は実機で主な道だけ（Move の距離と角度・確定で単位つき・Scale の鎖の絵・Shear の垂直＝本体と行列一致・Enter で OK・Escape で Cancel）＝インチ／パイカの文書・鎖を外した縦横別・-90/90 の端・負の拡大は試していない。Run Script は見送り。→（2026-10-23 ブロック20 の再検査・実機）インラインの物の行の Arrange は灰色（1行なら理由の違う語は出ない）／非表示のスプレッドの物の Lock は書けてアラートなし（OBJ-23）／何も変わらなかった書き込み（Center Content）と本体がエラーで止めた書き込み（Move 100000pt）は Undo に段を残さない（KohakuGraph で見た空の段は、KFC の1文書の巻き戻しでは出ない）／Move 999999999pt・Scale 999999999% は `beyond the pasteboard`・400桁の数字は窓の警告（D20-1）。
- ✅（2026-10-23 作者の判断 J20-1＝本体の部品の窓に作り直して解消＝本体の測る欄は Ciceros も `1p6` も読む・OBJ-20 の訂正）（以下はその時の記録）⬜（2026-10-23 ブロック20 の再検査・限界）OBJ-20：値の窓が知る単位は pt mm cm in pc px Q H の8つだけ＝定規が Ciceros・Agates・American Points・U・Bai・Mils・Custom の文書では「数字だけなら前面の文書の定規の単位」が pt になる（欄も `0 pt` と出す＝Ciceros で実測）・パイカの `1p6` の書き方は受けない＝作者の判断 J20-1（本体の部品の窓にすれば本体と同じ単位・書き方になる）。
- ⬜（2026-10-17・実機は一部だけ）OBJ-19：断る行のうち実機で見たのは「文字の枠でない」だけ。鍵の掛かった物・ロックしたレイヤー・あふれの中の物・もうアウトラインにした行・開けない章は設計と静的な確かめだけ。アンカー付き／インラインの枠（アウトラインが本文の中に入る）・連結した枠（本体は連結を切るか）・グループの中の文字の枠・表のセルの中の枠は試していない。回帰のケースはまだ無い（全件の回に足す）→書き込みの文は `objsp-outline-doc`・`objsp-outline-book`（2026-10-23）。
- ✅（2026-10-11 全件の点検で確かめた）OBJ-05：表のセルのグラフィックは出る。ボタン・複数ステートは、それ自体ではなく中の長方形が出る（本体の `findObject` と同じ4件）。置換は本体の「すべてを置換」と同じく、中の長方形・そのステートのグループ・ボタンや複数ステート自身にも濃淡が入る（回帰 `obj-kinds`）。
- ⬜（2026-10-18 から上限は 1000）OBJ-05：回帰 `obj-cap-301`（文書の 301 個）は 300 で止まる期待のまま＝今は 301 行全部が並ぶ＝期待を新しい答えにし、1000 を踏むケース（1001 個）を足す（全件の回）。1000 で止まることは実機で確かめた（OBJ-22 の F-4・1100 枠）。
- OBJ-05：上限（300 行）は、文書の 301 個で回帰 `obj-cap-301` が踏んでいる（2026-10-11 全件の点検で確かめた）。ブックで上限に当たる形（Find Object Format が空の大きなブック）は試験では見ていない。
- OBJ-10 ⑦：「着かない」は試験用ビルドの故障スイッチ `object-walk-miss` でだけ見た（文書を編集すると、先に指紋で断るので、編集では作れない）。
- OBJ-12：指紋の時間（1つあたり）は KIDMCP の verify の実測（473個を約0.3秒）からの見積もりで、KFC の一覧の検索では測っていない。
- OBJ-13：` (K partially)` が出る場面（本体が一部だけ置き換える物）は、試験では作っていない。
- ✅（2026-10-11 全件の点検で確かめた）OBJ-14：オブジェクトスタイルだけを条件にしたクエリは、回帰 `oqs-object-style`（2026-10-10 作者の依頼）で回っていて合格している。
- ✅（2026-10-11 全件の点検で確かめた）OBJ-03：グループの中の物だけを選ぶ＝その物だけ（`obj-selection-group-child`）、インラインだけを選ぶ＝その物だけ（`obj-selection-inline`）、グループそのものを選ぶ＝0件＝中の物を歩かない（本体の「検索と置換」も「0 オブジェクトが見つかりました」・`Group.findObject()` も0＝`obj-selection-group`）。
- ✅（2026-10-13 未明に回した）OBJ-12b：まとめての置換の後の取り直し（`obj-f2-rows-together`＝行を本物の Ctrl+クリックでまとめて選ぶ）は合格＝まとめて置換した後に、選ばなかった行を1行で置換しても断られない。本物のクリックを使うほかの物のケース（まとめて選ぶ・まとめて置換する）も合格。
- ⬜（2026-10-13 夜の直し・実機は未測）OBJ-08：アイテム名の40文字の切り口が前の字に付く記号の手前に来ない。回帰＝`obj-cut-mark-name`（45文字の名前・41文字目に濁点）＝全件の回に初めて回す（期待は設計から書いた）。
- OBJ-12b：アンカー付きの物（カスタムの位置）の取り直しは試験していない（作りはインラインと同じく、物から上がって出会う包みに聞く。アンカー付きの物の包みも `kInlineBoss` かは実測していない＝回したのはインラインの `obj-f2-inline`・`obj-f2-thread` と表のセルの `obj-f2-cell`）。
- ⬜（2026-10-14 の直し・実機は未測）物の行の locked：インライン・アンカー付きの物は、錨の枠のレイヤーで読む（NK8-4）。回帰＝`obj-anchored-locked`／`-armed`＝全件の回に初めて回す。
- ⬜（2026-10-14 新しい知識の回で拾った・実機は未測）OBJ-13：本体のオブジェクトの「すべてを置換」が**ロックした物**（ロックしたレイヤー・Object > Lock）を書くかは測っていない。KFC は検索の設定の「ロックしたレイヤー／ストーリーを含める」（本体のヘッダーは「探すだけ・置き換えはしない」）を、行の置換と同じく Change All の歩きにも渡す。行の置換は自分でロックを断る（OBJ-10 ④）が、Change All は本体に任せる＝本体のダイアログの「すべてを置換」と同じなら問題は無い。章に**置き換え先のオブジェクトスタイルが無い**とき（KFC が「置き換え先がある」を聞くのはセッションの設定だけ）に本体が作るか・飛ばすか・失敗するかも測っていない（ヘッダーの読み直し 101 の問い Q1・Q2＝`docs/ai-notes/hdr-reread-own-kfc140-2026-10-10.md` §3・案件に届いていなかった）。文字の Change All のロックは測ってある（`qs-locked`＝本体はロックした枠に書かない）。
