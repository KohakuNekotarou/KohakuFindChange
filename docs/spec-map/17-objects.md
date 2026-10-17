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
  並ぶ数の上限は文字と同じ（300 行＝GEN-34・FIND-37 の文）。
  **Find Object Format が空でも探す**＝その種類のフレームが全部並ぶ（本体の「次を検索」と同じ）。読むだけで、文書を「変更あり」にしない。（O5）
  - 訂正:

- **OBJ-06** ★**一覧の検索は、利用者の選択を元に戻す** ―― 本体のオブジェクトの検索は見つけた物を選択してしまう（窓の無い文書の物は、前面の文書の「同じ番号の別の物」を選んだ＝実測）ので、
  検索の前に選択を写し、終わったら戻す（文字の検索サービスの写し）。選択とアクティブなレイヤーは検索の前と同じ（実測 M4）。
  ★**何も選んでいなかったときは、何も選んでいない状態に戻す**（2026-10-10 作者の報告「オブジェクトスタイルでの検索結果 オブジェクトを選択された状態で始まります」）＝
  写しは「何も選んでいない」を戻さない（本体が最後に見つけた物が選ばれたまま残る）ので、検索の前に選択の有無を聞いておき、無かったら写しを戻した後に選択を外す。
  （塗りの検索で外れていたのは、最後に見つけた物が別のページにあったため＝試験用ビルドの記録 OBJWALK／OBJSEL・2026-10-10。回帰＝`obj-keepsel-style-none`）
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
  ・`(n)` は同じページに2つ以上あるときの順番（文字の行と同じ＝ROW-11）。親ページの物は親ページの名前と `master`（例 `PA master`）。
  ・状態の語の並び＝置き場所（master・grouped・inline・anchored）が先、届かない理由（hidden＝非表示のレイヤーか物そのものが非表示・locked＝オブジェクト > ロックかロックされたレイヤー）が後（計画の訂正 8）。省略しない（ROW-14）。
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
  ・**あふれた文字の中の物**（`overset` の行＝OBJ-08）は何も選ばず、その文字があふれ出るフレームの「＋」へ画面を動かし（文字の行のあふれと同じ＝JMP-11）、メッセージ欄に `That object is in overset text - not selected.`（2026-10-10 夜）。
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
  `Special Operations ▸` の中は本体の Object メニューの並び（ROW-32）：`Create Outlines`／─／`Fitting ▸`（Fill Frame Proportionally・Fit Content Proportionally・Content-Aware Fit・Fit Frame to Content・Fit Content to Frame・Center Content）／`Arrange ▸`（Bring to Front・Bring Forward・Send Backward・Send to Back）／`Transform ▸`（Rotate 90 Degrees Clockwise・Counter-clockwise・Rotate 180 Degrees・Flip Horizontal・Flip Vertical／─／Redefine Scaling as 100%・Clear Transformations）／─／`Lock`・`Unlock`・`Hide`・`Show`／─／`Move to Active Layer`・`Clear Object Style Overrides`／─／`Delete`。Show・90°／180°・反転・Clear Transformations は私が対として足した。名前は本体の英語のコマンド名（`.fr` は ASCII の約束＝`°` の代わりに `Degrees`）。サブメニューの名前は KFC の文字列キー（素の「Fitting」は本体のキー＝日本語の本体では日本語で出た・2026-10-17 実測）。
  どの行に＝Create Outlines と同じ（右クリックした行がまとめて選んだ行の1つなら選んだ行すべて・REP-36）。**スプレッドをまたいでも、文書（ブックの章）をまたいでも1回で**、全部で**取り消しの1段**（Edit メニューの名前は操作の英語名＝`Undo Fit Content Proportionally` など）。KFC が開いた章に書いたら窓を出す（Replace と同じ）。
  書き方＝本体の Facade（`IFrameContentFacade`・`IArrangeFacade`・`ITransformFacade`・`IPageItemLockFacade`・`IPageItemVisibilityFacade`・`ILayerFacade::MoveToLayer`・`IObjectStylesFacade::ApplyObjectStyleToUIDList`・`IPageItemScrapFacade`）を**スプレッドごとに1回**（本体のコマンドが受ける選択の単位＝2つのスプレッドの物を1回で渡すと MoveToLayer が2つ目のスプレッドの物を1つ目へ動かした・2026-10-17 実測で直した）。回転・反転は物ごとに、その物の中心で（スプレッドをまたぐ選択の中心は意味が無い）。
  断る行（理由ごとに数える）＝物の行でない・文書が見つからない／開けない・もう無い（行は Missing）・あふれの中・**鍵の掛かったレイヤーの物**（全部の操作）・**Object > Lock の物**（Unlock 以外）・**本体の `Can…` が「できない」**（例＝`no content to fit`・`in front already`・`at the back already`・`at 100% already`・`not transformed`・`cannot be locked`）・Unlock＝`not locked`・Hide＝`hidden already`・Show＝`not hidden`・Move to Active Layer＝`in a group or in text`（本体も一番外の物だけ）・`no layer named "…"`（その文書に同じ名前のレイヤーが無い＝作らない）・`on "…" already`・Clear Object Style Overrides＝`no object style`。**検索の後で物が変わった（指紋）では断らない**（OBJ-19 と同じ）。
  ★**変わった物だけに印**＝書いた後に物のデータ（指紋）が動かなかった物（Arrange は重なりの位置が動かなかった物）は `unchanged` と数え、行は変えない。⚠本体の Fit Content Proportionally・Fill Frame Proportionally は、もう合っている枠でも画像のデータを書き直す＝「1 object」と数える（Center Content・Fit Frame to Content は `unchanged`）。
  書いた行＝頭に操作の語（`Fitted`・`Arranged`・`Rotated`・`Flipped`・`Redefined`・`Reset`・`Locked`・`Unlocked`・`Hidden`・`Shown`・`Moved`・`Cleared`・`Deleted`）・**目玉が付く**（ROW-45）・行の場所（ページ・`locked`・`hidden`・`grouped` など）は**書いた後の物で読み直す**（ページが変わったら同じページの中の番号は消す）・指紋も書いた後のもの＝その後の `Replace` がそのまま通る。**`Replace` は灰色にしない**（物は同じ物のまま）＝`Delete` だけ灰色（物が無い・行は `(R/N)` の R に数える）。Replace・Create Outlines を後からかけると、行の語はそれ（`Changed`・`Outlined`）になる。探し直しで同じ物が見つかったら、見つけた行が語と目玉を受け継ぐ。
  Move to Active Layer の対象＝**前面の文書の Layers パネルで選ばれているレイヤー**（`ILayerUIUtils::QueryContextActiveLayer`＝本体の Layers パネルのメニューが対象を取る口）の**名前**。ほかの文書・章は同じ名前のレイヤーへ。前面に文書が無いときは灰色。
  文＝`Fit Content Proportionally: 2 of 8 objects - ID:266, ID:302. Left as they were: 6 no content to fit.`／`Move to Active Layer: 5 of 8 objects to "Images" - ID:266, .... Left as they were: 2 in a group or in text, 1 on "Images" already.`／`Lock: 9 of 11 objects in 2 documents - kfc-sp-a.indd ID:266, ...; kfc-sp-b.indd ID:278, .... Left as they were: 2 cannot be locked.`／書ける行が無い＝`<操作名>: none of the 3 selected rows can be changed - ...`・1行なら `<操作名>: <理由の文>`・書いたが何も変わらない＝`<操作名>: nothing was changed - 1 unchanged.`。取り消し＝`Undo: Send to Back - the rows are back as they were before it.`（UNDO-02）。
  ★**灰色＝選んだ行のどれもできないとき**（SET-11＝灰色でも項目は出す）＝上の扉と本体の `Can…` を読むだけで問う（文書を開かない・閉じた章の行は押せる）。例＝もう一番前の物の行で Bring to Front・Bring Forward が灰色、鍵の掛かった物の行で Unlock だけ押せる。
  ✅ 実機（2026-10-17 夜・テスト用ビルド・KIDMCP）＝2つのスプレッドの画像の枠に Fit Content Proportionally を一度に（ほかの6行は `no content to fit`）・Fitting の6つ・Send to Back（Ctrl+Z／Redo＝重なりの位置で追う）・90°回転（中心で・DOM で -90°）・左右反転・Redefine Scaling as 100%（150%→100%）・Clear Transformations・Lock／Unlock・Hide／Show・Clear Object Style Overrides（塗りと線の上書き→スタイルの値）・Delete（Ctrl+Z で物も行も戻る・Redo）・Move to Active Layer（直した後＝2つのスプレッドの物がそれぞれのページのまま・グループの中2つと既に Images の1つを数えて断る・レイヤーの無い文書の行は断る）・スプレッドをまたいだ Send to Back と Delete・閉じた2章のブックを Select All Rows で Lock（2文書1段・章に窓・どちらかの Ctrl+Z で両方戻り行も戻る）・鍵の掛かった物の行のメニュー。
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- ⬜（2026-10-17 夜・実機は主な道だけ）OBJ-20：試していない＝All Documents で2つの文書の行をまとめて選んだ書き込み（KIDMCP の `click_widget` に修飾キーが無い＝ブックの Select All Rows で2文書を測った）・インライン／アンカー付きの物（回転・削除で本文が動く）・マスターの物・非表示のレイヤーの物・300行の上限の近く・灰色の判定の重さ（行が多いときの右クリック）・回帰のケース（全件の回に足す）。値の要る Move／Scale／Rotate／Shear と Run Script は設計書の段2・段3（未実装）。
- ⬜（2026-10-17・実機は一部だけ）OBJ-19：断る行のうち実機で見たのは「文字の枠でない」だけ。鍵の掛かった物・ロックしたレイヤー・あふれの中の物・もうアウトラインにした行・開けない章は設計と静的な確かめだけ。アンカー付き／インラインの枠（アウトラインが本文の中に入る）・連結した枠（本体は連結を切るか）・グループの中の文字の枠・表のセルの中の枠は試していない。回帰のケースはまだ無い（全件の回に足す）。
- ✅（2026-10-11 全件の点検で確かめた）OBJ-05：表のセルのグラフィックは出る。ボタン・複数ステートは、それ自体ではなく中の長方形が出る（本体の `findObject` と同じ4件）。置換は本体の「すべてを置換」と同じく、中の長方形・そのステートのグループ・ボタンや複数ステート自身にも濃淡が入る（回帰 `obj-kinds`）。
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
