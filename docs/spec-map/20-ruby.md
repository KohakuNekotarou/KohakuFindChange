# 20. ルビで探す・ルビを付ける（Kohaku+ タブの Find: Ruby）

> コード地図での位置: `model/KFCRubySearch.*`（タブが今のタブか・歩く・行の字・読みの読み書き）・`model/KFCReplaceEngine.cpp` の `WriteOwnTabRows`（行の Replace＝Table タブと共通）・`ui/KFCKohakuTab.*`（タブの面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md`（R1〜R11）。各項目の末尾の（R<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/rb-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. タブと面

- **RUBY-01** ★本体の「検索と置換」の **Kohaku+ タブ**（21 章）の Find: を **Ruby** にしているとき（日本語で起動しているときだけ出る＝KOHAKU-03）、KFC の Find in … はルビの一覧を作る。閉じる時にそうなら閉じている間も同じ。（R1・K1）回帰 `rb-face-shown`・`rb-face-find`・`rb-face-closed`・`rb-face-done`・`rb-face-switch-back`
  - 訂正:

- **RUBY-02** 部品＝**Look For:**（Text With Ruby／Text Without Ruby／Ruby Marks in Text／Words in a Ruby List＝RUBY-60）・**Ruby:**（Without とルビの揺れのとき灰色＝RUBY-70）・**Base Text:**・**Change Ruby To の欄**（RUBY-33）・案内の2行。**Search:** は 5つ（KOHAKU-10）。★ラベルは右寄せで欄に付ける（作者 11-02「LookFor：とかのあとに空白があるのをなくしたい」＝欄の位置はそのまま・Kenten も同じ列）。★Ruby Marks in Text のときは欄を隠す（RUBY-50）。文字欄は1打鍵ごとに控える。値は KFC が覚える（起動している間だけ）。（R2・K5）回帰 `rb-face-base`・`rb-face-reading`・`rb-face-kind`・`rb-face-change`・`rb-face-empty`・`rb-face-scope`・`rb-part-face`
  - 訂正: 11-01 作者の試用＝Remove Ruby は無くした・Ruby を Base Text の上に・Change は□の欄に。★11-03 作者「LookForのリスト　日本語にして欲しい」＝一覧の字は日本語（★最終＝**ルビ有／ルビ無／ルビ印／ルビ一覧／ルビ揺れ**＝作者の名前〈「ルビの付いた文字 ルビ有 ルビ無 がいいかも」「ルビ印 ルビ一覧 ルビ揺れ にしましょうか」〉・その前は「ルビの付いた文字／ルビのない文字／ルビの印／ルビの一覧／ルビの揺れ」・KFC の言葉＝本体に無い）。★**ラベルも日本語**（作者 11-03「ChangeRubyto等を日本語にしたら その分狭くなるので 一つ一つのマスを大きく出来そう」＝検索対象：／ルビ：／親文字：／置換ルビ：／親文字：／検索のみ・一覧のときは 一覧：／対象：〈すべて・ストーリーで最初・ドキュメントで最初・ブックで最初〉／違う読みのルビも含める＝`ui/KFCJapanese.h`・ラベルの列は 65＝欄は 70 から）。★★11-04 最終＝作者の文字の絵「検索　検索のみ／ルビ／親文字／置換／ルビ／親文字」→「親文字を親だけに」→「：は他のと同じに」→「半角で:」＝**検索: [一覧▼] □検索のみ／ルビ:／文字:／置換:（見出し）／ルビ:／文字:**（作者「親のところは文字のほうがよさそう」「置換にも:つけておいてほしい」）・一覧のとき **一覧:／対象:**（半角の「:」＝面の上の Find: と Search: と同じ・圏点の部品も半角に）・**検索のみは検索の一覧の右**（前は欄の下の左＝どの検索対象でも同じ場所になった）・**ラベルの列は 30・欄は 33〜330**。置換の欄の行は「置換」の見出しの分だけ下（見出し 66・ルビ 86・親 110）。`.fr` の英語は順番と控えで、面を見せるときに書き込む（`ShowRubyKindEntries`・字は符号の数から＝.cpp は ASCII）。ラベル・伝言・段の名前は英語のまま（圏点の none と同じ）。回帰 `rb-face-entries` ★★★11-04 夜 作者「ルビ検索の件、文字を親文字に」＝**ラベルの「文字:」は「親文字:」**（検索側と置換側の両方＝圏点と同じ語 `KFCJa::LabelBase`・`LabelText` は外した）・「親文字:」が1字広いのでラベルの列は 30→42・欄と検索の一覧の左は 33→45。

- **RUBY-03** ★**How to Use...**＝ルビの書き方の窓（`KFCHowTo::ShowRubyMarks`・本文を選んでコピーできる）。日本語だけ（Ruby の種類は日本語の InDesign にしか出ない）。★**場所＝面の右の列の Find in KFC の下に縦に Choose List...・Save List...・How to Use...**（作者 11-02 夕「今完了ボタンのところにあるボタンを Choose Save Howto の並びで縦に並べる。他のタブと同じように」＝面の上から 180／211／242）＝Ruby の3つ。Choose List... は Words in a Ruby List でないとき灰色で残す（RUBY-60）。Kenten は How to Use... だけ（KENTEN-03）。他の種類のときは隠して使えなくする（隠しただけの部品はクリックを受ける）。回帰 `rb-frame-buttons`・`rb-howto`・`rb-howto-place`・`rb-howto-done`・`rb-howto-closed-elsewhere`・`kh-hidden-click-qr`・`kh-hidden-click-table`
  - 訂正: 11-02 昼は本体の枠の行（完了の左）に本体の普通のボタンを差し込んでいた（`QueryFrameButton`＝夕に撤去）。

- **RUBY-67** ★**親文字の□は入力が全部届いてから分ける**（作者 11-02 の不具合「琥珀猫をいれると 珀 猫 とはいる、琥がきえてしまう」「猫 琥 と入る」＝日本語入力が確定した字を1字ずつ同じ□へ渡し、2字目で分けたので3字目が先頭の□の頭に入っていた）。□の通知では字を覚えるだけで、分けるのは次の空き時間（`ICallbackTimer`）。キー・文字・日本語入力の知らせがまだ並んでいれば（`PeekMessage`）30ms、変換中なら（IMM）100ms 後に聞き直す。（I1〜I3）回帰 `rb-box-ime-commit`（確定を送る）・`rb-box-ime-real`（本物の日本語入力で打つ）・`rb-box-flow-keys`
  - 訂正:


## 2. 探す

- **RUBY-10** ★**Text With Ruby**＝ルビの strand の連続が1行（`IRubyAttrStrand::GetRubyRun`）。Base Text は親文字、Ruby は読みに「含まれる」か（そのまま比べる）。空は何でも。（R3）回帰 `rb-any`・`rb-reading`・`rb-base`・`rb-both`・`rb-none`
  - 訂正: ★11-01＝**Base Text があるとき、行＝その出現＋それに触れるルビの全部**（1字ずつ付けた太郎も、太にだけ付けた太るも1行＝付けた後も同じ条件で見つかる）。一字も触れていなければ行にしない。Base Text が空なら今のまま1本のルビが1行。Ruby: は行の読みをつないだ字（空白を除く）に含まれるか。（試用の直し F22）回帰 `rb-box-found-after`／★11-02 再検査＝Base Text が空で Ruby: があるときも、**接しているルビの読みはつないで比べる**＝1字ずつ付いた `太《た》郎《ろう》` が `たろう` で1行（行＝読みが掛かるルビの全部）・`ろう` なら郎だけ・離れたルビはつながない。回帰 `rb-reading-across`・`rb-reading-part`・`rb-reading-apart`

- **RUBY-11** ★**Text Without Ruby**＝Base Text の出現のうちルビが掛かっていないもの。Base Text が空なら断る。（R3）回帰 `rb-without`・`rb-without-nobase`
  - 訂正:

- **RUBY-12** 見えない読みはルビでない（脚注の印が制御文字を読みとして受け継ぐ）。隠した条件の中は出さない。本文・セル・脚注の物語、★親文字のページ・ロック・非表示はアイコンの範囲（RUBY-15＝11-02 に「全部の物語」から変えた）。上限 1000。（R4・R5）回帰 `rb-footnote`・`rb-hidden`・`rb-cell`・`rb-book`・`rb-alldocs`
  - 訂正:

- **RUBY-13** 段の名前＝`[Ruby/Document] Find:"漢字《かんじ》"`（Without は `Find:"漢字 (no ruby)"`・無条件は `[Any Ruby]`）。保存したクエリにはならない。クエリのダイアログの Find・Run はタブを譲る。（R10）回帰 `rb-yield-closed`・`rb-query-run-scope`
  - 訂正: ★11-03 作者「ぱねるのほうのFind""にはいる文字、日本語の文字にしておいて欲しい　ルビのゆらぎかな」＝**Find:"" の中は日本語・検索対象の名前を角かっこに**＝`[ルビ有]`（無条件）・`[ルビ無] 漢字`・`[ルビ印]`・`[ルビ一覧] 名前.txt（ストーリーで最初）（違う読みのルビも含める）`・`[ルビ揺れ] 太郎`（親文字と読みを聞いたときは今までどおり `漢字《かんじ》`）。圏点は `[すべての圏点]`・`漢字（圏点なし）`（KENTEN-13）。段の頭の `[Ruby/Document]` は英語のまま。★11-04＝ルビ有で**親文字だけ**を聞いたときは `[ルビ有] 太`（それまでは空の読みを聞いたように `太《》`＝私の気づきを作者が「おねがいします」・回帰 `rb-onechar-mono` の正解の行）。読みだけなら今までどおり `《たろう》`。

- **RUBY-14** アプリケーションバーの欄は空になり、Return で一覧を作る。回帰 `rb-appbar-enter`
  - 訂正: ★2026-11-07＝Ruby の種類でも、欄はテキストタブの検索文字列・Return はテキストの検索（QR-22 の訂正と同じ・BAR-12 の訂正）。回帰 `rb-appbar-enter`（期待を入れ替えた・Kenten の `kt-appbar-enter` も）

- **RUBY-15** ★**Search: の下のアイコン**（作者 11-02）＝本体のテキストタブと同じ7つ・同じ絵・同じ順＝ロックされたレイヤーとオブジェクト／ロックされたストーリー（どちらも検索のみ＝行はロックの印で Replace は灰色）／非表示のレイヤーとオブジェクト／親ページ／脚注／カナを区別／全角・半角を区別。初期値は本体と同じ（前の4つが切・後の3つが入）。Ruby と Kenten で別々に持つ・クエリに `switches`（ビット）・起動している間だけ。範囲の判定は本体の `doc.findText()` と同じ件数になることを文書ごと測った（`own-dom-count.jsx`・`KFCOwnScope`）＝ストーリーのロック→脚注は脚注のスイッチ→枠で親ページ・非表示・ロック。枠の無い文字（あふれ）は常に入る。文末脚注は脚注ではない。⚠ストーリーのロックは DOM から立てられず実測していない（InCopy の経路だけ）。回帰 `rb-scope-*`・`rb-switch-face`・`kq-pick-switches*`
  - 訂正:

- **RUBY-16** ★**カナ・全角半角を区別しないとき**＝本体と同じ字を同じと見る（Base Text・Ruby:・Ruby Marks の親文字と読み・リストの語・Change の親文字の欄）。規則は全文字を本体で測った（`model/KFCFold.h`＝ゕゖと繰り返し記号は畳まない・`"` `'` `` ` `` は全角と別・半角の濁点は1字にまとまる・ｲﾞｴﾞは別・`ｱﾞ` はアと同じ）。行は本文の字の範囲（`ｶﾞｸ` は3字）を指し、読みはその字に付く。大文字小文字は比べたまま（アイコンを足さない＝作者の一覧に無い）。回帰 `rb-fold-*`・単体 `work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1`
  - 訂正:

## 3. 行

- **RUBY-20** ★行は文字の行。一致の部分に `親文字《読み》`（長ければ切る＝親文字 20・読み 28 字）＝`Hit::runChain`。Without の行はふつうの字。着地・ページ・ロック・編集に付いていく・Copy・Clear はふつうの文字の行のまま。（R6）回帰 `rb-click`・`rb-next`・`rb-long`・`rb-copy`・`rb-clear`・`rb-edit-moved`・`rb-locked`・`rb-click-native-scope`
  - 訂正:

- **RUBY-21** ★（11-03 作者「ルビがきれる、なおしてほしい」＝一致が右端に寄った行で、親文字より広い読み〈郎の上の「ろう」〉が右端で切れていた→前の字を削るとき、読みが一致の始まりから届く所までの場所を空ける＝`KFCColorTextView` の upperReach。後ろの字は下の段だけなので読みの下に入ってよい）★**行は2行で描く**（作者 11-01「KCM の様に」）＝高さ 38px（他の一覧は 19px のまま）。下の段＝ふつうの行（場所・文脈・一致は親文字だけ）、上の段＝一致の真上に読み（同じ大きさ・全の濃さ）。複数の塊は塊ごとに真上（`太《た》郎《ろう》`）・横にあふれたら次を押す（重ねない）・モノルビを空白入りの1本で持つ行は字ごとに。Without・印の行も同じ高さ（Replace で読みが付いても高さを変えない＝KCM の理由）。ルビを消した行は上の段に棒。⚠部品の文字（KIDMCP が読む）と Copy は `[太郎《たろう》]` のまま。段（灰色の写し）も同じ見た目。行のリソース `kKFCResultTallHitNodeWidgetRsrcID`・`KFCResultListWidgetMgr::DrawsTall`（行の種類・リソース・高さは同じ問いの答え）・`ChangeRoot(kFalse)`。（F10〜F13）回帰 `rb-two-*`
  - 訂正:

## 4. Replace

- **RUBY-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**。読み＝`kTARubyAttrBoss`・`kTARubyStringBoss`（ルビの見た目は触らない）。**読みが全部空＝ルビの属性を全部外す**（30 個・KIDMCP の `ClearRubyOn` と同じ一覧と同じ `ClearOverridesCmd`）。スタイルはルビそのもの（フラグ・読み）を持てない（DOM に無い＝10-31 実測）＝スタイルの見た目は残す。回帰 `rb-remove-all`・`rb-style`・`rb-remove-keeps-style-look`・`rb-undo-remove-all`。物語に strand が無ければ先に作る。（R7）回帰 `rb-change-one`・`rb-change-all`・`rb-without-apply`・`rb-no-strand`・`rb-remove`・`rb-nothing`・`rb-return`・`rb-cell-change`・`rb-book-replace`
  - 訂正: 11-01 作者＝「Change に何も入っていなければ削除」＝Remove Ruby の箱は無くした。

- **RUBY-31** 断り＝既にそのとおり（数える＝書かない）／行の字が親文字の□と違う（`base text differs`）／読みの下に親文字が無い□があるのに他の□には親文字がある（全部の行を断る）／親文字が全部空で読みの数が1でも字数でもない。ロックされた行は灰色。（R7・F20）回帰 `rb-same`・`rb-locked`・`rb-box-differs`・`rb-box-over-nothing`・`rb-box-nobase-count`
  - 訂正:

- **RUBY-33** ★★**□の欄**（作者 11-01「□□□□□ のような入力欄に漢字やルビを入力／＋で増やせる／結合できる」・触った後の指示＝検索側も□・Change は連動）＝検索の段は読みの□（Ruby:）の下に親文字の□（Base Text:＝★**1字ずつ**＝2字以上を打つか貼ると次の□へ続く＝作者 11-02「複数の文字を入力したら自働的に次のマスに」・先の□の字は上書き・8列目を超えた分は8列目の□・キーボードは書いた最後の□へ移りカーソルは字の後ろ・分けるのは入力が全部届いてから＝RUBY-67）。Change の段は読みの□（Change Ruby To）の下に親文字の字（枠なし・編集できない＝検索の親文字の□の字）。列はそろう。★**列は8つで固定**（作者 11-02 夕「最初からこのサイズでマスは８個で固定にしましょうか、＋も-ボタンも無くす」＝大きさは12列だったときの□の幅）。隣り合う□の間の鎖＝読みの□がつながる（グループルビ）＝検索と Change が同じ形。読みの字は左へ寄せ、解くと左に残る。親文字の□は結合しない。灰色＝Text Without Ruby とルビの揺れ（11-03）のとき検索の読み（11-03 からラベルも）。Ruby Marks in Text と Words in a Ruby List では□も鎖もラベルも隠れる。⚠**探すときは□の字をつないで比べる**（モノルビとグループルビを区別しない＝私の判断）。（F14〜F17・L3）回帰 `rb-box-words`・`rb-box-one-char`・`rb-box-flow-keys`・`rb-box-flow-grow`・`rb-box-find-together`・`rb-box-follow`・`rb-box-group`・`rb-box-join-left`・`rb-box-eight`・`rb-box-marks-grey`・`rb-box-without-grey`
  - 訂正: 11-01〜11-02 昼は始め5列で＋と−で1〜12列に増減した（`rb-box-plus-minus` は 11-02 夕に外した）。★11-03 作者「８つこていにしましたが、これだとルビに入れる文字がみずらくなってしまう…しょき４つで＋ボタンを復活させましょうか-も 最少４かな？」＝**初め4列・＋と−で4〜8列**（＋は Ruby: の行の右端・−は Base Text の行の右端＝キャプション設定のアイコン・−は最後の列を中身ごと消す＝11-01 と同じ）・**欄の幅は列の数で 70〜304 を分ける**（4列で約57・8列で約27）・**打ちきれない字は見えている最後の欄**（作者「自動では増えない」を選んだ）・保存したクエリは使っている列まで開く（4未満なら4）。★★11-04 作者「では、４つにしましょう、＋-もなしで、そうするとさらにマスがおおきくなる」（間に「4〜6でお願いします」を挟んだ）＝4列固定・＋−なし・欄は 70〜330 を4つで分ける＝1欄約63px（＋−の場所も欄に）。作者の問い「５つにして、ひとマスに4文字はいるかな？　源が　みなもと」を写真から測った＝この窓の字は全角1字約12px（「検索のみ」4字で49px）⇒ラベルが 65 のままの5列（1欄約50px）では「と」が欠ける。★★★**最終＝5列固定・＋−なし**（作者「出来る限りルビなどの欄を左に寄せて　マスをおおきくできないかな？　それでマス５つにできるかな？」→ラベルを短く〈RUBY-02〉）＝欄は 33〜330 を5つで分け、間2px＝**1欄57〜58px**。実測（`box_text_width.ps1`＋`box_ink_measure.ps1`・テスト用ビルド 01:45）＝**「みなもと」は字46px・欄の左端から6px・右に5px＝欠けずに入る**（作者「素晴らしい、綺麗に入りましたね」）。作者は1つの入力欄（本体のルビの窓の形）も考えたが、送り仮名（太る）・グループとモノの混ざり（東京都）・先頭のかなを飛ばす（お客様）が1回で書けなくなるので「マスがあるほうがよさそう」。6字以上の語は5つ目の欄に残りの字が入り、そこへの読みは空白で区切れば1字ずつ（RUBY-34 の空白の決まり＝本体のルビの窓のモノルビの書き方）。**6〜8列で保存したクエリ**は、5列目より後の欄の親文字を5列目に足し、読みはその列（つながっていればその塊の先頭）の読みの後ろに空白を挟んで足す（1字ずつの読みは1字ずつのまま）・Ruby: の字は続けて足す（探す字は同じ）・親文字の無い読みは足さない。★灰色の間も検索のルビの欄の字は消さない（作者 11-04「入ったままでいいかも、検索のみにしたときも…色がかわるだけ」「ルビもはいったままで」＝ルビ無・ルビ揺れに切り替えても字は残り灰色になるだけ＝検索のみが置換の側を灰色にして残すのと同じ・同じ夜に一度「消す」を作って戻した）。回帰 `rb-face-columns`・`rb-box-each`・`rb-kind-keeps-ruby`。 ★★★11-04 夜 作者「マスは４つに」＝**4列固定**（＋−なしのまま）・欄は 45〜330 を4つで分ける＝1欄 69〜70px・5列目の部品（読み・親文字・置換の欄・鎖・置換の下の字）は外して ID を空けた・それより多い列で保存したクエリは、はみ出た字を最後の欄へ（今までと同じ規則）。 ★★★11-04 夜 作者「入力欄は、中央に配置は出来ない？」（マスの中の字を）→「はい」→ 中央寄せを作った（c21ba06）が、同じ日に作者「Windows でしかできないのなら SDK でできないのならやめる」→ SDK を調べ尽くして口が無いと確かめ（入力欄の口・資源の型・実機のボスの口 27・公開の Drover の部品・説明書・本体の入力欄 155 は全部左寄せ）**外した＝マスの字は左寄せのまま**（効いたのは Win32 の ES_CENTER だけ＝ノート docs/ai-notes/edit-box-text-alignment-2026-11-04.md）。

- **RUBY-34** ★**付け方**（`model/KFCRubyChangePlan.h`＝std だけで単体試験 `work/note-scripts/2026-11-01-kfc-kohaku-fixes/plan_test.ps1`）＝読みの□1つ＝1本のルビ、その下の親文字（結合なら全部の列）に付く。親文字が2字以上＝グループ（`kTAMojiRubyBoss` kFalse）。別々の□＝別々のルビ。空の読みの下の字はルビなし（`太る`＝`[ふと][  ]`／`[太][る]`）＝もともと付いていた字からは消す。親文字が全部空なら行の字全部に＝読みが1つならグループ・数が字数と同じなら1字ずつ。1つの読みに空白があって塊の数が字数と同じなら1字ずつ（本体のモノルビの書き方を貼られたとき）。（F18〜F21）回帰 `rb-box-each`・`rb-box-part`・`rb-box-nobase-each`・`rb-box-spaces`
  - 訂正: ★2026-11-04 作者「1字はモノルビで直してください」＝**親文字1字のルビは種類をはっきりモノ（`kTAMojiRubyBoss` kTrue）で書く**＝前にグループが付いていた字でもモノになる（それまでは種類を書かず、字のもとの種類のまま＝KFC の `Apply`・ルビ印・ルビ一覧・そろえるも同じ口）。2字以上はこれまでどおりグループ。回帰 `rb-onechar-mono`（monotwo の「太」＝1字のグループに「ふと」→mono・RED＝いまのビルドで group を確かめた）・`rb-change-one` に「漢字＝group」を足した。★空白の決まり（1つの読みに空白があって塊の数が字数と同じなら1字ずつ）は**残す**（作者「もともとのIndesignがそういう仕様なのですね…するとスペースのしくみはそのまま」・同じ夜に「＊１ てすと」のような空白の入ったルビのために一度消しかけて戻した＝`plan_test.ps1` に作者の例を足して 38件 PASS）。

- **RUBY-35** クエリの形＝`rubyChange`（Change の読み）・`rubyChangeBase`（親文字）・`rubyFind`（検索の読み）を□ごとにタブ区切りで（`ruby`・`rubyBase` はつないだ字＝探す条件）。試用中に作られた古い形（`rubyChange` 1つ・`rubyRemove`）も読む。（F23）回帰 `kq-save-boxes`・`kq-pick-ruby`・`kq-pick-remove`
  - 訂正:

- **RUBY-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Change Ruby`）。行は `Changed`＋書いた後の字（置換済みの灰色にはしない＝もう一度書ける）。取り消しで行の字も戻り、やり直しでまた `Changed`。（R8）回帰 `rb-undo`・`rb-undo-remove`
  - 訂正:

- **RUBY-68** ★**面のボタン**（作者 11-02「FindNextなどのぼたん Ruby、圏点の方にもつけれるかな？」）＝Find Next・Change・Change All・Change/Find が Table と同じ働き（TBL-60〜65）。Find Next＝Search: の範囲の行（Look For の種類のまま＝リスト・印・揺れも）を一覧と同じ順に、選択から先へ。1行目＝`<字>《<読み>》 - page <ページ>`。Change＝見つけた箇所に Change 側（□の読み・リストの読み・印の読み＝行の Replace と同じ書き方＝`KFCReplaceEngine::WriteFacePlaces`）・Change All＝範囲の全部（どれも Ctrl+Z 1回）。押せるのは Table と同じ条件（Text Without Ruby の Change 系は読みの□に字があるとき）。ロックは書かずに数えて言う。アラートは本体の文（`Search is completed. N replacement(s) made.`／`Cannot find a match.`）。（B1〜B5）回帰 `rb-btn-next`・`rb-btn-next-from-selection`・`rb-btn-change`・`rb-btn-change-grey-after-move`・`rb-btn-changefind`・`rb-btn-changeall`・`rb-btn-changeall-locked`・`rb-btn-none`・`rb-btn-list`・`rb-btn-marks`・`rb-btn-closed-doc`・`rb-btn-ja`
  - 訂正:

- **RUBY-71** ★**Find Only**（作者 11-03「圏点は検索だけのができますよね、ルビはできないですよね、ルビも検索で置換しないというのができるようにしてほしい。ちぇっくぼたん？」・置き場所は「FindOnly　左側に欲しい」）＝Ruby の部品のチェック（既定は切）。★★11-04＝**名前は「検索のみ」・検索の一覧の右・ルビ有とルビ無のときだけ出る**（作者「るび印とルビ一覧　検索のみのちぇっつくいらないかも」「るびゆれもかな」）＝ほかでは隠れ、チェックは覚えて効かない（`KFCRubyConditions::FindsOnly()`＝書く口・灰色・段の文・クエリの Run の判定は全部これ）・回帰 `rb-findonly-place`（ルビ有で x=228 に出て、ルビ一覧・ルビ印で隠れる）。以下は 11-03 の形（どの Look For でも効いた）。入れると Look For のどれでも書かない＝行の Replace・Return は書かずに `Replace: Find Only is on for Ruby on the Kohaku+ tab - turn it off to change, in Edit > Find/Change.`（書き手3つ＝□・印・リストの `NothingToChange`＝その時の面で読む）／面の Change・Change All・Change/Find は灰色（`ChangeSideAsks`＝押しても書かない＝`WriteFacePlaces` も断る）／Change Ruby To のラベル・□・下の親文字は灰色。Find Next と Find in … は変わらない（見つけた箇所も残る＝切ると Change がまた押せる）。置き場所＝部品の左端（ラベルの列）で、部品が見せているものの下（□の下＝部品の 118・リストの3行の下＝76・印のときは一番上）。部品は 21 高くなった＝面の2行は 306／323（終わり 340＝本体の面 356 から余白 15 を引いた中）。クエリに保存（`rubyFindOnly`・前の形は切）＝Run Saved Queries の Run は飛ばして `nothing to change to`（`HasChangeSide`＝QR Code・Warichu と同じ）。回帰 `rb-findonly-replace`・`rb-findonly-marks`・`rb-findonly-list`・`rb-findonly-face`・`rb-findonly-place`・`kq-pick-ruby-findonly`・`kq-save-roundtrip-ruby-findonly`・`kq-qd-run-ruby-findonly`（対照 `kq-qd-run-ruby-findonly-off`）
  - 訂正:


## 5. 効かない操作

- **RUBY-40** 灰色と扉は Table タブの TBL-40 と同じ。動く＝Copy・Clear・Search This Document Again・Search These Results Again（その一覧の探した条件で）。段から戻すと同じ行。Change All in Book は灰色。（R9・R10）回帰 `rb-menus`・`rb-search-again`・`rb-doc-again`・`rb-tier-back`・`rb-changeall-refused`
  - 訂正:

- **RUBY-41** 作っていないもの＝読みの GREP・ルビの見た目の検索と置換・青空文庫以外の印の書き方（`漢字（かんじ）` など）。（R11）
  - 訂正:

## 6. 原稿の印からルビ（Ruby Marks in Text）

- **RUBY-50** ★`Find:` の3つ目。青空文庫の書き方＝`｜漢字《かんじ》`（バーは全角・半角）、バーが無ければ《の直前の漢字（々〆〇ヶを含む）。読みが空・行をまたぐ・親文字が無いものは印でない。★□の欄は隠れ、入っている字では絞らない（作者 11-02「入力欄は必要なのかな？いらなければ、非表示にできれば非表示に」＝読みは印にあり、省きたい印は行を Replace しなければよい）。行は印の範囲（字はふつうの行のまま）。段の名前＝`[Ruby/Document] Find:"[Ruby Marks]"`。（R12）回帰 `rb-marks-no-boxes`・`rb-marks-list`・`rb-marks-not`・`rb-marks-base`・`rb-marks-reading`・`rb-marks-face`
  - 訂正: ★2026-11-07 作者の決定（判断待ち⑨・「そのように」→問いに「青空文庫の決まりに広げる」）＝**バーが無ければ、《の直前に続く同じ種類の字が親文字**＝漢字（々〆〇ヶを含む）・ひらがな（ゝゞを含む）・カタカナ（ー・ヽヾを含む）・全角の英数字・半角の英数字（`KFCRubyMarkText::BaseKindOf`）。記号・空白・中黒・半角カナの直後の《》は印でない（バーが要る）。ルビ印もルビ一覧の行も同じ口（`MarkAtOpen`）。⚠書名の括弧に《》を使う本文も、直前がかなや英字なら印として一覧に出る＝行を選ばずに Replace（How to Use のルビの窓に1文）。回帰＝`rb-marks-kinds`・`rb-marks-kinds-replace`・`rb-marks-not`（あ《x》は印になった）・単体 `list_test.ps1` の L1b。

- **RUBY-51** ★Replace＝読みを親文字に付け（グループルビ）、印を文字から消す。Change 側は見ない（欄は隠れる）。文字が動くので同じ文書の他の行を運ぶ（`MarksWrite::WriteChapter`＝`OutlineInChapter` の型）。書いた行は親文字に立ち、`Changed`＋`親文字《読み》`。もう一度は「既にそのとおり」。全部で Ctrl+Z 1回＝文字も行も戻る。（R12）回帰 `rb-marks-replace-one`・`rb-marks-replace-all`・`rb-marks-undo`・`rb-marks-again`・`rb-marks-cell`・`rb-marks-book`
  - 訂正:

- **RUBY-52** ★Track Changes が入っている物語は断る（消した字が物語に残るので、段ごと戻して文で言う）。実測＝自分の変更履歴の挿入を消すのは記録に残らず本当に消える（その場合は書ける）。回帰 `rb-marks-track`
  - 訂正:

## 7. ルビのリスト（Words in a Ruby List）

> 設計書 `docs/superpowers/specs/2026-11-02-kfc-ruby-list-design.md`（L1〜L11）。コード＝`model/KFCRubyList.h`（std だけ・単体試験 `work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1`）・`model/KFCRubyListFile.*`（読む）・`KFCRubySearch` の `WalkList`・`KFCReplaceEngine.cpp` の `RubyListWrite`。

- **RUBY-65** ★**Occurrences:**（作者 11-02「それずっと欲しかった」「りすとでえらぶのかな、Storyで初出とか」「documentで初出とか、ブックで初出とか」）＝All／First in Story／First in Document／First in Book。語（リストの行）ごとに最初の1か所だけ行にする。最初＝一覧に並ぶ順の最初（ページ順・同じページは歩いた順・あふれは最後）＝表のセルや脚注は本文より後ろに持っているので物語の中の順では決めない。最初の1か所にルビがあればその語の行は出さない（数を言う）。見る範囲は Search: の範囲とアイコンの範囲の中。Book Scope が切の First in Book は First in Document と同じ。`Search This Document Again` は First in Book のとき断る（前の章を読まない）。段の名前に `(First in Document)`。クエリに `rubyFirst`。実装＝`KFCRubySearch.cpp` の `WalkList`（スレッドごとの最初を候補に）→`KeepFirsts`。回帰 `rb-first-*`・`kq-pick-first`・`kq-qd-run-ruby-list-first`・`kq-qd-run-ruby-list-all`
  - 訂正:

- **RUBY-66** ★**Save List...**（作者 11-02・面の右の列＝RUBY-03・Ruby のとき Look For によらず出る）＝Search: の範囲（Book Scope ならブック）のルビをリストのファイルに書き出す。文書は変えない。同じ語は1行・接したルビは1語・★同じ語の読みが割れたら、語に次の1字を付けて1行ずつ書き分ける（作者 11-02「読みが割れた語だけ次の1字を付けて」＝`太《ふと》る`・`太《た》田`）。読みが1つの語・読みが同じで付き方だけ違う語（モノとグループ）には付けない。次の字が空白・段落の終わり・隠れた字などなら語だけの行。できた語が同じになったら（同じ次の字・既にある長い語）多い方を行に（同数は先に出た方）、他は `# another reading, N of M places:` の行。行に書けない読み（印を含む等）は `# no line can hold this` で残す＝書く前に全行を読み戻して確かめる（`KFCRubyList::ListMaker`）。アイコンの範囲に従う。UTF-8（BOM つき）・CRLF・既定の名前 `<文書名> Ruby List.txt`。何も無ければファイルを作らず言う。⚠章を閉じる前に文書の参照を全部手放す＝`rb-save-book` で 11-02 に保護シャットダウン（`CloseDocCmd - document is still referenced`）を踏んだ。回帰 `rb-save-*`
  - 訂正:

- **RUBY-60** ★Look For: の4つ目。**Choose List...**（面の右の列＝RUBY-03・他の Look For では灰色＝作者 11-02）でファイルを選ぶ＝1行1語・Ruby Marks in Text と同じ印で書く（`太《た》郎《ろう》`・`太《ふと》る`・`漢字《かんじ》`・`｜食べ物《たべもの》`）。印を除いた字を探し、印の字にだけ付ける（`太《ふと》る`＝太るを探して太にだけ＝作者 11-02）。ファイルでは `＜＜ ＞＞`・`<< >>`・`≪ ≫` も印。空行と `#` の行は飛ばす・CSV とタブ区切りは1列目・UTF-8／UTF-16／Shift_JIS。□の欄は隠れ、`Ruby List:` の右にファイル名、面の1行目に「N words.」か断りの文（KOHAKU-37）。★11-04 作者「ルビ一覧のときコメントが表示されますが、日本語にして欲しい」＝**面の文は日本語**＝「5語を読みました。」／「Choose List... でファイルを選んでください。」／「ファイルを読めません。」／「UTF-8・UTF-16・Shift_JIS のどれでもありません。」／「3行目に読みがありません。」／「3行目の《》の前に、漢字か｜がありません。」／「3行目の語に、1行目と違う読みがあります。」（おかしい行が2つ以上なら「（おかしい行は全部でN行）」）／「語がありません。」＝ファイル名は「一覧:」の横にあるので文に入れない。モデルは事実だけ返す（`IKFCRuns::RubyListFacts`＝`KFCRubyListWhy`・境界の版 45）・パネルの文は英語のまま。★同じ回に**ルビ印の面にも説明**（作者「ルビ印の時にもコメント欲しいかも」）＝面の2行に「本文の｜漢字《かんじ》のような印を探します。」「Replace や Change で、親文字にルビを付けて印を消します。」（ほかの文が出ている間は出さない・Look For を変えると消える）。回帰 `rb-list-msg`・`rb-list-msg-leaves`・`rb-marks-face`。（L1〜L4・L7・L10）回帰 `rb-list-face`・`rb-list-cp932`・`rb-list-msg`
  - 訂正: ★2026-11-07（判断待ち⑨＝青空文庫の決まり・RUBY-50 の訂正）＝読めない印の行の文は「3行目の《》の前に、漢字・かな・英数字か｜がありません。」（パネルの英語＝`a reading needs kanji, kana, letters or digits just before it, or a bar where its base starts.`）。

- **RUBY-61** ★読めない行が1行でもあればファイルを使わない＝最初の行の番号を言う（印が無い／読みが読めない／同じ語に違う読み＝両方の行）。ファイルを選んでいなければ断る。（L3・L5）回帰 `rb-list-nofile`・`rb-list-bad-line`・`rb-list-bad-mark`・`rb-list-other-reading`
  - 訂正: ★2026-11-07（判断待ち⑨＝青空文庫の決まり）＝`かな《かな》` の行は読める語になった（`rb-list-kana-word`）。読めない印の例は `「猫」《ねこ》`（`rb-list-bad-mark` のリストを替えた）。

- **RUBY-62** ★Find in … ＝語の出現を全部（Occurrences: が All のとき＝RUBY-65）。**長い語が勝つ**（ファイルの順によらない）。行の上の段に**付く読みを薄く**。段の名前＝`[Ruby/Document] Find:"[Ruby List] 名前.txt"`。★すでにルビが1字でも掛かっている出現は行にせず数を言う（`N already had ruby and are not listed.`＝上書きしない）。隠した条件・削除・ノートの中は出さない。選択範囲は丸ごと入るものだけ。上限 1000。（L6・L9）回帰 `rb-list-find`・`rb-list-longest`・`rb-list-selection`・`rb-list-selection-cut`
  - 訂正:

- **RUBY-63** ★行の Replace・Return＝行の語の読みを書く（Change の□と同じ書き方＝2字以上はグループ・1字は種類を書かない）。Change 側は見ない。全部で Ctrl+Z 1回。**行は一覧を作ったときの語で書く**＝ファイルを後で直しても一覧は変わらない（Search Again も同じ語）。行の字がリストの語でなくなっていれば `edited since`。（L8・L11）回帰 `rb-list-replace-one`・`rb-list-replace-all`・`rb-list-types`・`rb-list-undo`・`rb-list-again`・`rb-list-file-changed`
  - 訂正:

- **RUBY-64** 保存したクエリはファイルのパスを持つ。Run のときに読み、読めなければそのクエリを飛ばして言う（`the file could not be read`）。一覧には載らない（QRY-92）。回帰 `kq-qd-run-ruby-list`・`kq-qd-run-ruby-list-bad`
  - 訂正:

- **RUBY-69** ★**Include Text With Other Ruby**（Words in a Ruby List のチェック・作者の案②「リストと違う読みも出す」＝11-02「作ってみて欲しい」）＝リストと違う読みのルビが付いた語も行にする。行は `今のルビ → リストの読み`（例 `太郎《たろ》 → 太《た》郎《ろう》`）。行の Replace・面の Change でリストの読みに書き直す。リストと同じ読みのルビ（□をつないで比べる＝モノとグループを区別しない・カナと全角半角はアイコンに従う）と、語の外まで続くルビは行にせず数える（`N already had ruby reading as the list does, or running past the word, and are not listed.`）。切ると今までどおり（ルビのある語は数えるだけ）。クエリに保存（`rubyOther`）。段の名前に `(With Other Ruby)`。（P2-1〜P2-4）回帰 `rb-list-other-off`・`rb-list-other-on`・`rb-list-other-same-reading`・`rb-list-other-replace`・`kq-pick-ruby-other`・`kq-qd-run-ruby-list-other`・`kq-save-roundtrip-ruby-list`
  - 訂正:

## 8. ルビの揺れ（Inconsistent Ruby）

- **RUBY-70** ★Look For: の5つ目 **Inconsistent Ruby**（作者の案③「ルビの揺れを探す」＝11-02「作ってみて欲しい」・行はふつうのルビの行＝Replace でそろえられる）＝Search: の範囲（Book Scope ならブック全体で数える）で、**同じ親文字に2通り以上の読みがある語**の出現を全部行にする。語は Save List... と同じ切り方（接したルビは1語）。読みは□をつないで比べる（モノとグループを区別しない・カナと全角半角はアイコンに従う）。★**見出しは読みごと**（作者 11-03「たろう　その子供にたろうで見つかった場所かな」＝文書の下のストーリーの段の代わりに `<語>《<読み>》 (0/N)`・語が最初に出る順・同じ語は数の多い読みから＝モデルの `BuildReadingGroups`・ブックは章ごと＝数はその章の中）。行の字（コピー・KIDMCP が読む字）は今までどおり `<字>《<読み>》  (<読み> <数>, …)`（ブック全体の数・絵には出ない＝2段の行の下の段は字だけ）。Base Text の□で絞れる（その字を含む語だけ・数は絞らずに範囲全体）。★**検索の Ruby: の□はラベルごと灰色で、残っていた字も読まない**（作者 11-03「検索のときのRubyのぶぶんは必要ないということですよね　そうであるなら、そこは灰色、入力できなくしてほしい」＝読みを含む箇所で絞っても外れた読みだけは取り出せない・見えない条件にしない・段の名前にも出さない＝`WalkVaries`）。★**置換側は無い**（作者 11-03「これ、いっそのこと置換のルビもなくしてもいいかもですね」・行の Replace と Return は「灰色にする」を選んだ）＝Change Ruby To の行を隠す・行の Replace は灰色・Return は書かずに `Inconsistent Ruby: a word takes one of its readings by the right-click on its heading or on its row.`・面の Change／Change All／Change/Find は灰色・保存したクエリの Run は飛ばす（`nothing to change to`）・そろえるのは RUBY-72 の右クリックだけ（前は行の Replace・面の Change＝Change Ruby To の□の読みだった）。段の名前 `[ルビ揺れ]`（Base Text があれば後ろに・11-03 に日本語＝RUBY-13）。クエリに保存（`rubyLook=4`）。（P3-1〜P3-4）回帰 `rb-varies-find`・`rb-varies-base`・`rb-varies-replace`・`rb-varies-book`・`kq-save-roundtrip-varies`・`rb-varies-reading-ignored`・`rb-varies-reading-grey`
  - 訂正:

- **RUBY-72** ★**「《読み》にそろえる」**（作者 11-03「たろのほうを　たろうにかえたければ　子供や　題名のところの右クリックで置換すると変わる」「子供の方はリターンも」・(b)「右クリックに同じ語のほかの読みを並べる」を選んだ）＝ルビの揺れの見出しと行の右クリックに、その語のほかの読みを1つ1項目（数の多い順・最大6・Replace の下＝出す直前に足す＝`PopRowMenu`・名前は描くときに **`《たろう》`**＝作者 11-04「たろうにそろえる　となっていますが、　<<たろう>> だけがいいかな」・11-03 は `《たろう》にそろえる`）。★そろえた行は **(R/N) の R に数える**（作者 11-04「そろえても　０/1 とかの０のところのすうじがかわらない」＝ROW-06 の数がタブの書いた行＝表・ルビ・圏点の `Changed` を数えていなかった・行の Replace も同じだった＝`KFCResultModel.cpp` の `CountedAsWritten`・特別な操作の印＝Cleared ほかは数えない）＝実機 11-04 01:15 テスト用ビルド：「太郎《たろ》 (0/2)」の見出しで《たろう》→ **(2/2)**・文書と段 **(2/9)**・取り消しで **(0/2)・(0/9)** に戻る（`unify_count_check.ps1`）。回帰 `rb-varies-unify-head`・`rb-varies-unify-row`・`rb-change-one`・`rb-marks-replace-one`・`kt-change-one`・`tb-replace-one`（数の期待を足した）。見出し＝その下の行を全部／行＝その行（いっしょに選んだ行のうちなら選んだ行の同じ語のもの）を、その読み1つで行の字全体に（Change Ruby To の□1つ・Base Text なしと同じ書き方＝`KFCReplaceEngine::UnifyRubyHits`）。Ctrl+Z 1回（段の名前 `Change Ruby`）。断り＝Replace と同じ（Find Only・ロック・既にそのとおり）。行の Replace・Return は今までどおり Change Ruby To の□。読みの字はページのとおり（数えた最初の箇所の字＝たたまない）。ほかの一覧の行には出ない。境の版 44（口 `IKFCResults::GetUnifyFacts`・`IKFCRuns::UnifyRubyRows`）。回帰 `rb-varies-heads`・`rb-varies-unify-menu`・`rb-varies-unify-head`・`rb-varies-unify-row`・`rb-varies-unify-undo`・`rb-varies-unify-findonly`・`rb-varies-unify-none`
  - 訂正:
