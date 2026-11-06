# 27. 行の物のスクリプトラベルのキーを付ける・書き換える・外す（Special Operations ▸ Label）

> コード地図での位置: `ui/KFCLabelDialogs.*`（3つの窓・キーの一覧）・`ui/KFCResultNodeEH.cpp` の `PopRowMenu`（子メニューを行ごとに足し外し）・`ui/KFCActionComponent.cpp` の `DoLabel`（押す・灰色）・`ui/KFCResultListWidgetMgr.cpp` の `LabelRows`・`model/KFCReplaceEngine.cpp` の `CanLabelHit`／`LabelKeysOfRows`／`WriteLabelRows`・`model/KFCScriptLabelSearch.cpp` の `HolderLabels`。
> 設計書 `docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md`（LB1〜LB6）。回帰の表＝`work/kbs-regress/lbl-cases.tsv`（作る台本＝`work/note-scripts/2026-11-06-kfc-label-spelling/lbl_cases.py`）。
> 作者の依頼（2026-11-06）「検索したStoryやObjectに対してSPOPでラベルを付けたい。Keyを付ける。Keyを削除（Keyがなければ選べない）。Contextを編集の３つかな？Label子メニューから３つが選べる感じ どれもモーダルダイアログかな。UIは Contextを編集は Keyのリストが有って そのしたにContext入力欄かな」「Special Operations ▸ Label　のメニュー名は貴方のおすすめで（英語が分からないので私は）」「あとは、おすすめで」。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. メニュー

- **LBL-01** ★**行の右クリックの Special Operations ▸ Label ▸ Add Key... / Edit Value... / Remove Key...**（ABC 順。名前は作者が私に任せた＝スクリプトの `insertLabel(key, value)` の言葉）。出る行＝**ラベルを持てる物の行**＝物の行（Object タブ・QR Code・Script Label の物の行）・Story の行（内部ストーリーも）・Kohaku+ の Table の行・Script Label の表とセルの行。**文字の一致の行・表の行の一覧（List Rows of These Tables）・クエリの実行の一覧の行には出ない**（右クリックのたびに行ごとに足し外す）。回帰 `lbl-menu-object`・`lbl-menu-text-none`
  - 訂正:

- **LBL-02** **Edit Value... と Remove Key... は、選んだ行の物にキーが1つも無ければ灰色**（Add Key... は物があればいつも押せる）。ブックの閉じた章の行で読めないものがあるときは灰色にしない＝押したときに開いて聞く。走っている間・着地の間は全部灰色。回帰 `lbl-menu-story-grey`
  - 訂正:

- **LBL-03** 今までの **Remove Label**（Script Label の一覧の行だけ・Find Label: の一致を外す＝SL の章）は別のものとしてそのまま。
  - 訂正:

## 2. 窓

- **LBL-10** ★**窓は本体の部品**（Move／Scale の窓と同じ方針）＝題 `Kohaku Find/Change - Add Key`／`- Edit Value`／`- Remove Key`、右に OK・Cancel。回帰 `lbl-add-window`・`lbl-edit-window`・`lbl-remove-window`
  - 訂正:

- **LBL-11** **Add Key...**＝`Key:`（1行）と `Value:`（複数行）。Key が空なら OK で断る（`Type a key.`）。Value が空でも断る（`Type a value - InDesign keeps no key with an empty value.`）。回帰 `lbl-add-object`・`lbl-add-replace`
  - 訂正:

- **LBL-12** **Edit Value...**＝`Keys:` の一覧（選んだ行の物が持つキー＝`<キー>  (n of N)`＝何個の物が持つか。スクリプトラベルパネルのラベルは `[Script Label panel]`）と `Value:`（複数行）。キーを選ぶと値が欄に入る。物ごとに値が違えば注 `Its values differ among the items - what you type replaces them all.`、そのキーを持たない物があれば注 `Not every item has this key - the others are left as they are.`。★**空の値＝キーを外す**（注 `An empty value takes the key away.`）。キーを選ばずに OK なら断る（`Pick a key.`）。回帰 `lbl-edit-two`・`lbl-edit-panel`
  - 訂正:

- **LBL-13** **Remove Key...**＝`Keys:` の一覧から1つ選んで OK。回帰 `lbl-remove-status`・`lbl-remove-panel`
  - 訂正:

- **LBL-14** キーの一覧はクエリの窓の一覧と同じ部品（平らな一覧・1つ選ぶ・行のツールチップ＝キーの全文）。
  - 訂正:

## 3. 書き込み

- **LBL-20** ★**窓1回＝Ctrl+Z 1回**（取り消しの名前 `Add Label Key`／`Edit Label Value`／`Remove Label Key`）。書く口は本体のスクリプトラベルの口（`IScriptUtils::SetScriptingTag`）＝スクリプトの `insertLabel` と同じ。回帰 `lbl-add-undo`
  - 訂正:

- **LBL-21** 状態行＝`Add Key: Added "<キー>" to <物の言い方>.`／`Remove Key: Removed "<キー>" from ….`／`Edit Value: Set "<キー>" on ….`。もう持っていた物があれば ` (N had it already - its value replaced.)`。スクリプトラベルパネルのラベルは `the Script Label panel's label` と言う。回帰 `lbl-remove-status`
  - 訂正:

- **LBL-22** ロックした物（ロックしたレイヤー・ロックした物・ロックしたストーリー）は書かずに数えて言う。回帰 `lbl-locked`
  - 訂正:

- **LBL-23** ブックの閉じた章の行は、他の特殊操作と同じく開いて書き、閉じる。キーを読むのも開いて読む（読んでいる間は他の操作を断る）。回帰 `lbl-book-closed`・`lbl-book-closed-keys`
  - 訂正:

- **LBL-24** Story の行＝そのストーリー、Table の行＝その表、セルの行＝そのセルに書く。回帰 `lbl-story-add`・`lbl-table-add`・`lbl-cell-add`
  - 訂正:

- **LBL-25** Script Label の一覧（Find: Script Label）の行に書いたら、その行の印を付け直す（一覧の条件のキーを外したら Removed など）。
  - 訂正:

---

## ⬜ この章で未確認

- 内部ストーリー（XML の保管場所）に書けるか・保存して開き直したときに残るか（設計書の M3＝実測待ち）。
- 実機の全件（`lbl-cases` 19 件）は未回し（2026-11-06 時点＝試験を書いてビルド待ち）。
