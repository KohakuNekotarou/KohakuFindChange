# 21. 検索と置換の「Kohaku+」タブ（QR Code・Table・Ruby・Kenten を1枚に）

> コード地図での位置: `ui/KFCKohakuTab.*`（面）・`model/KFCSearchEngine.cpp` の `PartStretches`（範囲）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-kohaku-tab-design.md`（K1〜K23）。各項目の末尾の（K<番号>）は設計書の決定。回帰の表＝`work/kbs-regress/kh-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. タブ

- **KOHAKU-01** ★本体の「検索と置換」のタブの行に、KFC のタブは **`Kohaku+` の1枚**（本体の6枚と合わせて7枚＝日本語版の窓の幅で切れない）。（K1）回帰 `kh-one-tab`
  - 訂正:

- **KOHAKU-02** ★面の上に **Find:**（QR Code／Table／Ruby／Kenten）と **Search:**。その下に選んだ種類の部品だけが出る。`Find Next` は QR Code のときだけ右上（窓を別の種類で開いてから QR Code にしても押せる）。（K2）回帰 `kh-part-shown`・`kh-findnext-after-another-kind`
  - 訂正:

- **KOHAKU-03** ★**Ruby と Kenten は日本語で起動しているときだけ** Find: に出る（他の言語では QR Code・Table の2つ）。（K11）回帰 `kh-japanese`・`kh-not-japanese`・`kh-not-japanese-on-ruby`
  - 訂正:

- **KOHAKU-04** Find: の値に応じて、パネルのタイトル（`Kohaku Find/Change - Table - Document`）と Find の名前、Find in … の動きが**その場で**変わる。本体のタブへ移れば本体のタブに戻り、戻れば前の種類のまま。閉じても開き直しても同じ。（K3・K4）回帰 `kh-kinds-title`・`kh-kinds-find`・`kh-closed-kind`・`kh-leave-back`
  - 訂正:

- **KOHAKU-05** 隠れている部品はクリックも Tab キーも受けない。文字欄に打っている途中で Find: を変えても、次のキーは隠れた欄に入らない。（K2）回帰 `kh-hidden-click-qr`・`kh-hidden-click-table`・`kh-hidden-click-ruby`・`kh-focus-left`・`kh-tab-keys-qr`・`kh-tab-keys-table`
  - 訂正:

- **KOHAKU-06** 保存したクエリの Find でタブが譲った後も、Find: を選び直せばその種類に戻る。（K4）回帰 `kh-yield-kind`
  - 訂正:

## 2. Search:

- **KOHAKU-10** ★**Search: は種類で共通の1つ**。Table・Ruby・Kenten は `All Documents／Document／Story／To End of Story／Selection`、QR Code は `All Documents／Document`。本体のタブの Search: とは別の値。（K5）回帰 `kh-scope-entries`・`kh-scope-shared`・`kh-scope-native-unmoved`
  - 訂正:

- **KOHAKU-11** Story などを選んだまま QR Code にすると `Document` と見せて探す。種類を戻すと元の値。（K5）回帰 `kh-scope-story-qr`
  - 訂正:

- **KOHAKU-12** ★5つは**いつも並ぶ**（本体の一覧は選択で増減するが、こちらは人の選んだ値を保つ）。今の選択で選べない値ならドキュメント全体を探し、メッセージで言う（Text と同じ文）。タイトルと Find の名前は「これから探す範囲」を言う。（K6）回帰 `tb-part-nothing`・`tb-part-caret-selection`・`tb-part-title-nothing`・`tb-part-face-kept`
  - 訂正:

- **KOHAKU-13** ★範囲の中身は**本体の範囲そのもの**。表＝文字（錨）が区間の中、または区間がその表のセルの中。Story＝その物語の全部の表。To End of Story＝カーソルより後ろの表（カーソルがセルの中なら、その表とその後ろの表＝本体もセルから物語の終わりまで歩く＝本体の「すべてを置換」で測った）。Selection＝選択の中の表（セルを選んでいればその表）。（K7）回帰 `tb-part-story`・`tb-part-toend`・`tb-part-selection`・`tb-part-selection-two`・`tb-part-selection-none`・`tb-part-frame`・`tb-part-cell-story`・`tb-part-cell-toend`・`tb-part-cell-text`・`tb-part-cells`
  - 訂正:

- **KOHAKU-14** ルビ・圏点の連なりは範囲に触れていれば行になる。Text Without…・ルビの印は範囲に丸ごと入っていれば行になる。（K7）回帰 `rb-part-*`・`kt-part-*`
  - 訂正:

- **KOHAKU-15** 範囲の一覧の Search These Results Again は断る（`the search was over part of the document (Search: Story) - search again.`）。Search This Document Again は灰色。行の Replace は効く。Book Scope ON のとき Document 以外は断る（Text と同じ文）。（K8）回帰 `tb-part-again`・`tb-part-doc-again`・`tb-part-replace`・`tb-part-book`・`rb-part-again`・`kt-part-again`
  - 訂正:

## 3. 面の並び

- **KOHAKU-20** `Find:`・`Search:` のラベルの横に空きを作らない。`Find Table Style:`・`Change to Table Style:` は語の下に一覧。Ruby・Kenten はラベルと欄の横並び。（K12）
  - 訂正:

## 4. クエリ（作者 10-31「公式ダイアログで選択可能？ 選択するとそれぞれのタブが開いて設定される」＝ `ui/KFCKohakuQueryMenu.h`・本体の欄の仕組みと実測＝`docs/ai-notes/kfc-native-query-menu-2026-10-31.md`）

- **KOHAKU-30** ★Kohaku+ タブの条件はクエリとして保存でき、**本体の検索と置換のクエリの欄に並ぶ**。ファイル＝`Find-Change Queries\<種類> - <名前>.xml`（フォルダ直下＝本体の欄の最後のグループ・[カスタム] の直前。KFC のフォルダは作らない）。欄の文字＝`Table - 名前`。回帰 `kq-listed`
  - 訂正:

- **KOHAKU-31** ★欄で選ぶ＝Kohaku+ タブが開き、Find: と条件が入り、欄がその名前を言う。本体のタブの中身（Text の検索文字列など）には触らない。タブが出ているときはその場で入れ替わる。パネルの Find と行の Replace はその条件で動く。本体のクエリを選べば本体のタブへ戻る。回帰 `kq-pick-table`・`kq-pick-ruby`・`kq-pick-kenten`・`kq-pick-remove`・`kq-pick-qr`・`kq-pick-keeps-text`・`kq-pick-on-tab`・`kq-pick-then-native`
  - 訂正:

- **KOHAKU-32** 面の条件を手で変える＝欄は [カスタム] になる（本体のタブと同じ）。`Search:` はクエリに入らない（本体の `検索:` と同じ＝変えても欄は変わらない）。回帰 `kq-custom-on-change`
  - 訂正:

- **KOHAKU-33** ★Kohaku+ タブで本体の保存ボタンを押す＝KFC の名前の窓（`Query Name:`）。名前の規則は本体と同じ（空・全部空白・`" * / : < > ? \ |`）。同じ種類に同じ名前があれば `Replace it?`（既定は No）。保存すると欄に並び、その行が選ばれる。本体のタブでの保存ボタンは本体のまま。回帰 `kq-save`・`kq-save-roundtrip`・`kq-save-replace`・`kq-save-replace-no`・`kq-save-chars`・`kq-save-cancel`・`kq-save-native-untouched`
  - 訂正:

- **KOHAKU-34** 本体の削除ボタン＝そのクエリのファイルを消す（本体のまま）。回帰 `kq-delete`
  - 訂正:

- **KOHAKU-35** KFC が入っていない InDesign でその行を選ぶ＝空のカラーのクエリが読まれるだけ（害なし）。回帰 `kq-pass`（故障スイッチ `query-menu-pass`）
  - 訂正:
