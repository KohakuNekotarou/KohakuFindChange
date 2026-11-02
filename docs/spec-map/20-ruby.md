# 20. ルビで探す・ルビを付ける（Kohaku+ タブの Find: Ruby）

> コード地図での位置: `model/KFCRubySearch.*`（タブが今のタブか・歩く・行の字・読みの読み書き）・`model/KFCReplaceEngine.cpp` の `WriteOwnTabRows`（行の Replace＝Table タブと共通）・`ui/KFCKohakuTab.*`（タブの面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md`（R1〜R11）。各項目の末尾の（R<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/rb-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. タブと面

- **RUBY-01** ★本体の「検索と置換」の **Kohaku+ タブ**（21 章）の Find: を **Ruby** にしているとき（日本語で起動しているときだけ出る＝KOHAKU-03）、KFC の Find in … はルビの一覧を作る。閉じる時にそうなら閉じている間も同じ。（R1・K1）回帰 `rb-face-shown`・`rb-face-find`・`rb-face-closed`・`rb-face-done`・`rb-face-switch-back`
  - 訂正:

- **RUBY-02** 部品＝**Look For:**（Text With Ruby／Text Without Ruby／Ruby Marks in Text／Words in a Ruby List＝RUBY-60）・**Ruby:**（Without のとき灰色）・**Base Text:**・**Change Ruby To の欄**（RUBY-33）・案内の2行。**Search:** は 5つ（KOHAKU-10）。文字欄は1打鍵ごとに控える。値は KFC が覚える（起動している間だけ）。（R2・K5）回帰 `rb-face-base`・`rb-face-reading`・`rb-face-kind`・`rb-face-change`・`rb-face-empty`・`rb-face-scope`・`rb-part-face`
  - 訂正: 11-01 作者の試用＝Remove Ruby は無くした・Ruby を Base Text の上に・Change は□の欄に。

- **RUBY-03** ★**完了ボタンの左に How to Use...**（作者 11-02「完了ボタンの左に欲しい」）＝ルビの書き方の窓（`KFCHowTo::ShowRubyMarks`・本文を選んでコピーできる）。日本語だけ（Ruby の種類は日本語の InDesign にしか出ない）。完了は本体の枠の部品で面の外にあるので、面が出たときに本体の枠の行へ本体の普通のボタンを差し込み（`ui/KFCKohakuTab.cpp`＝`QueryHowTo`）、Ruby と Kenten の間だけ見せる。面が隠れるときは持っているボタンを隠すだけで、部品はたどらない（QR の D1）。回帰 `rb-howto`・`rb-howto-place`・`rb-howto-done`・`rb-howto-closed-elsewhere`
  - 訂正:

## 2. 探す

- **RUBY-10** ★**Text With Ruby**＝ルビの strand の連続が1行（`IRubyAttrStrand::GetRubyRun`）。Base Text は親文字、Ruby は読みに「含まれる」か（そのまま比べる）。空は何でも。（R3）回帰 `rb-any`・`rb-reading`・`rb-base`・`rb-both`・`rb-none`
  - 訂正: ★11-01＝**Base Text があるとき、行＝その出現＋それに触れるルビの全部**（1字ずつ付けた太郎も、太にだけ付けた太るも1行＝付けた後も同じ条件で見つかる）。一字も触れていなければ行にしない。Base Text が空なら今のまま1本のルビが1行。Ruby: は行の読みをつないだ字（空白を除く）に含まれるか。（試用の直し F22）回帰 `rb-box-found-after`／★11-02 再検査＝Base Text が空で Ruby: があるときも、**接しているルビの読みはつないで比べる**＝1字ずつ付いた `太《た》郎《ろう》` が `たろう` で1行（行＝読みが掛かるルビの全部）・`ろう` なら郎だけ・離れたルビはつながない。回帰 `rb-reading-across`・`rb-reading-part`・`rb-reading-apart`

- **RUBY-11** ★**Text Without Ruby**＝Base Text の出現のうちルビが掛かっていないもの。Base Text が空なら断る。（R3）回帰 `rb-without`・`rb-without-nobase`
  - 訂正:

- **RUBY-12** 見えない読みはルビでない（脚注の印が制御文字を読みとして受け継ぐ）。隠した条件の中は出さない。本文・セル・脚注・マスターの全部の物語。上限 1000。（R4・R5）回帰 `rb-footnote`・`rb-hidden`・`rb-cell`・`rb-book`・`rb-alldocs`
  - 訂正:

- **RUBY-13** 段の名前＝`[Ruby/Document] Find:"漢字《かんじ》"`（Without は `Find:"漢字 (no ruby)"`・無条件は `[Any Ruby]`）。保存したクエリにはならない。クエリのダイアログの Find・Run はタブを譲る。（R10）回帰 `rb-yield-closed`・`rb-query-run-scope`
  - 訂正:

- **RUBY-14** アプリケーションバーの欄は空になり、Return で一覧を作る。回帰 `rb-appbar-enter`
  - 訂正:

## 3. 行

- **RUBY-20** ★行は文字の行。一致の部分に `親文字《読み》`（長ければ切る＝親文字 20・読み 28 字）＝`Hit::runChain`。Without の行はふつうの字。着地・ページ・ロック・編集に付いていく・Copy・Clear はふつうの文字の行のまま。（R6）回帰 `rb-click`・`rb-next`・`rb-long`・`rb-copy`・`rb-clear`・`rb-edit-moved`・`rb-locked`・`rb-click-native-scope`
  - 訂正:

- **RUBY-21** ★**行は2行で描く**（作者 11-01「KCM の様に」）＝高さ 38px（他の一覧は 19px のまま）。下の段＝ふつうの行（場所・文脈・一致は親文字だけ）、上の段＝一致の真上に読み（同じ大きさ・全の濃さ）。複数の塊は塊ごとに真上（`太《た》郎《ろう》`）・横にあふれたら次を押す（重ねない）・モノルビを空白入りの1本で持つ行は字ごとに。Without・印の行も同じ高さ（Replace で読みが付いても高さを変えない＝KCM の理由）。ルビを消した行は上の段に棒。⚠部品の文字（KIDMCP が読む）と Copy は `[太郎《たろう》]` のまま。段（灰色の写し）も同じ見た目。行のリソース `kKFCResultTallHitNodeWidgetRsrcID`・`KFCResultListWidgetMgr::DrawsTall`（行の種類・リソース・高さは同じ問いの答え）・`ChangeRoot(kFalse)`。（F10〜F13）回帰 `rb-two-*`
  - 訂正:

## 4. Replace

- **RUBY-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**。読み＝`kTARubyAttrBoss`・`kTARubyStringBoss`（ルビの見た目は触らない）。**読みが全部空＝ルビの属性を全部外す**（30 個・KIDMCP の `ClearRubyOn` と同じ一覧と同じ `ClearOverridesCmd`）。スタイルはルビそのもの（フラグ・読み）を持てない（DOM に無い＝10-31 実測）＝スタイルの見た目は残す。回帰 `rb-remove-all`・`rb-style`・`rb-remove-keeps-style-look`・`rb-undo-remove-all`。物語に strand が無ければ先に作る。（R7）回帰 `rb-change-one`・`rb-change-all`・`rb-without-apply`・`rb-no-strand`・`rb-remove`・`rb-nothing`・`rb-return`・`rb-cell-change`・`rb-book-replace`
  - 訂正: 11-01 作者＝「Change に何も入っていなければ削除」＝Remove Ruby の箱は無くした。

- **RUBY-31** 断り＝既にそのとおり（数える＝書かない）／行の字が親文字の□と違う（`base text differs`）／読みの下に親文字が無い□があるのに他の□には親文字がある（全部の行を断る）／親文字が全部空で読みの数が1でも字数でもない。ロックされた行は灰色。（R7・F20）回帰 `rb-same`・`rb-locked`・`rb-box-differs`・`rb-box-over-nothing`・`rb-box-nobase-count`
  - 訂正:

- **RUBY-33** ★★**□の欄**（作者 11-01「□□□□□ のような入力欄に漢字やルビを入力／＋で増やせる／結合できる」・触った後の指示＝検索側も□・Change は連動）＝検索の段は読みの□（Ruby:）の下に親文字の□（Base Text:＝★**1字だけ**）。Change の段は読みの□（Change Ruby To）の下に親文字の字（枠なし・編集できない＝検索の親文字の□の字）。列はそろう。始めは5列・＋で増やし−で減らす（検索側だけ・最大 12・最小 1・列の幅は窓の幅を列の数で割る）。隣り合う□の間の鎖＝読みの□がつながる（グループルビ）＝検索と Change が同じ形。読みの字は左へ寄せ、解くと左に残る。親文字の□は結合しない。灰色＝Text Without Ruby のとき検索の読み／Ruby Marks in Text のとき Change の段と鎖。⚠**探すときは□の字をつないで比べる**（モノルビとグループルビを区別しない＝私の判断）。⚠`.fr` に上限の数を置いて出し入れする（`LayOutRubyGrid`＝`SetFrame`・`ShowView`・`Enable`・窓の大きさを動かさない）。（F14〜F17）回帰 `rb-box-words`・`rb-box-one-char`・`rb-box-find-together`・`rb-box-follow`・`rb-box-group`・`rb-box-join-left`・`rb-box-plus-minus`・`rb-box-marks-grey`・`rb-box-without-grey`
  - 訂正:

- **RUBY-34** ★**付け方**（`model/KFCRubyChangePlan.h`＝std だけで単体試験 `work/note-scripts/2026-11-01-kfc-kohaku-fixes/plan_test.ps1`）＝読みの□1つ＝1本のルビ、その下の親文字（結合なら全部の列）に付く。親文字が2字以上＝グループ（`kTAMojiRubyBoss` kFalse）。別々の□＝別々のルビ。空の読みの下の字はルビなし（`太る`＝`[ふと][  ]`／`[太][る]`）＝もともと付いていた字からは消す。親文字が全部空なら行の字全部に＝読みが1つならグループ・数が字数と同じなら1字ずつ。1つの読みに空白があって塊の数が字数と同じなら1字ずつ（本体のモノルビの書き方を貼られたとき）。（F18〜F21）回帰 `rb-box-each`・`rb-box-part`・`rb-box-nobase-each`・`rb-box-spaces`
  - 訂正:

- **RUBY-35** クエリの形＝`rubyChange`（Change の読み）・`rubyChangeBase`（親文字）・`rubyFind`（検索の読み）を□ごとにタブ区切りで（`ruby`・`rubyBase` はつないだ字＝探す条件）。試用中に作られた古い形（`rubyChange` 1つ・`rubyRemove`）も読む。（F23）回帰 `kq-save-boxes`・`kq-pick-ruby`・`kq-pick-remove`
  - 訂正:

- **RUBY-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Change Ruby`）。行は `Changed`＋書いた後の字（置換済みの灰色にはしない＝もう一度書ける）。取り消しで行の字も戻り、やり直しでまた `Changed`。（R8）回帰 `rb-undo`・`rb-undo-remove`
  - 訂正:

## 5. 効かない操作

- **RUBY-40** 灰色と扉は Table タブの TBL-40 と同じ。動く＝Copy・Clear・Search This Document Again・Search These Results Again（その一覧の探した条件で）。段から戻すと同じ行。Change All in Book は灰色。（R9・R10）回帰 `rb-menus`・`rb-search-again`・`rb-doc-again`・`rb-tier-back`・`rb-changeall-refused`
  - 訂正:

- **RUBY-41** 作っていないもの＝読みの GREP・ルビの見た目の検索と置換・青空文庫以外の印の書き方（`漢字（かんじ）` など）。（R11）
  - 訂正:

## 6. 原稿の印からルビ（Ruby Marks in Text）

- **RUBY-50** ★`Find:` の3つ目。青空文庫の書き方＝`｜漢字《かんじ》`（バーは全角・半角）、バーが無ければ《の直前の漢字（々〆〇ヶを含む）。読みが空・行をまたぐ・親文字が無いものは印でない。Base Text・Ruby で絞れる。行は印の範囲（字はふつうの行のまま）。段の名前＝`[Ruby/Document] Find:"[Ruby Marks]"`。（R12）回帰 `rb-marks-list`・`rb-marks-not`・`rb-marks-base`・`rb-marks-reading`・`rb-marks-face`
  - 訂正:

- **RUBY-51** ★Replace＝読みを親文字に付け（グループルビ）、印を文字から消す。Change 側は見ない（欄は灰色）。文字が動くので同じ文書の他の行を運ぶ（`MarksWrite::WriteChapter`＝`OutlineInChapter` の型）。書いた行は親文字に立ち、`Changed`＋`親文字《読み》`。もう一度は「既にそのとおり」。全部で Ctrl+Z 1回＝文字も行も戻る。（R12）回帰 `rb-marks-replace-one`・`rb-marks-replace-all`・`rb-marks-undo`・`rb-marks-again`・`rb-marks-cell`・`rb-marks-book`
  - 訂正:

- **RUBY-52** ★Track Changes が入っている物語は断る（消した字が物語に残るので、段ごと戻して文で言う）。実測＝自分の変更履歴の挿入を消すのは記録に残らず本当に消える（その場合は書ける）。回帰 `rb-marks-track`
  - 訂正:

## 7. ルビのリスト（Words in a Ruby List）

> 設計書 `docs/superpowers/specs/2026-11-02-kfc-ruby-list-design.md`（L1〜L11）。コード＝`model/KFCRubyList.h`（std だけ・単体試験 `work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1`）・`model/KFCRubyListFile.*`（読む）・`KFCRubySearch` の `WalkList`・`KFCReplaceEngine.cpp` の `RubyListWrite`。

- **RUBY-60** ★Look For: の4つ目。**Choose List...**（右上の列の2段目）でファイルを選ぶ＝1行1語・Ruby Marks in Text と同じ印で書く（`太《た》郎《ろう》`・`太《ふと》る`・`漢字《かんじ》`・`｜食べ物《たべもの》`）。印を除いた字を探し、印の字にだけ付ける（`太《ふと》る`＝太るを探して太にだけ＝作者 11-02）。ファイルでは `＜＜ ＞＞`・`<< >>`・`≪ ≫` も印。空行と `#` の行は飛ばす・CSV とタブ区切りは1列目・UTF-8／UTF-16／Shift_JIS。□の欄は隠れ、ファイル名と「N words.」か断りの文が出る。（L1〜L4・L7・L10）回帰 `rb-list-face`・`rb-list-cp932`
  - 訂正:

- **RUBY-61** ★読めない行が1行でもあればファイルを使わない＝最初の行の番号を言う（印が無い／読みが読めない／同じ語に違う読み＝両方の行）。ファイルを選んでいなければ断る。（L3・L5）回帰 `rb-list-nofile`・`rb-list-bad-line`・`rb-list-bad-mark`・`rb-list-other-reading`
  - 訂正:

- **RUBY-62** ★Find in … ＝語の出現を全部（初出だけは作らない）。**長い語が勝つ**（ファイルの順によらない）。行の上の段に**付く読みを薄く**。段の名前＝`[Ruby/Document] Find:"[Ruby List] 名前.txt"`。★すでにルビが1字でも掛かっている出現は行にせず数を言う（`N already had ruby and are not listed.`＝上書きしない）。隠した条件・削除・ノートの中は出さない。選択範囲は丸ごと入るものだけ。上限 1000。（L6・L9）回帰 `rb-list-find`・`rb-list-longest`・`rb-list-selection`・`rb-list-selection-cut`
  - 訂正:

- **RUBY-63** ★行の Replace・Return＝行の語の読みを書く（Change の□と同じ書き方＝2字以上はグループ・1字は種類を書かない）。Change 側は見ない。全部で Ctrl+Z 1回。**行は一覧を作ったときの語で書く**＝ファイルを後で直しても一覧は変わらない（Search Again も同じ語）。行の字がリストの語でなくなっていれば `edited since`。（L8・L11）回帰 `rb-list-replace-one`・`rb-list-replace-all`・`rb-list-types`・`rb-list-undo`・`rb-list-again`・`rb-list-file-changed`
  - 訂正:

- **RUBY-64** 保存したクエリはファイルのパスを持つ。Run のときに読み、読めなければそのクエリを飛ばして言う（`the file could not be read`）。一覧には載らない（QRY-92）。回帰 `kq-qd-run-ruby-list`・`kq-qd-run-ruby-list-bad`
  - 訂正:
