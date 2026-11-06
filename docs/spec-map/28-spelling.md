# 28. 波線の語を一覧にする（Kohaku+ タブの Find: Spelling）

> コード地図での位置: `model/KFCSpellingSearch.*`（歩く・語の切り出し・種類の語・自動スペルチェックの ON/OFF）・`model/KFCFaceText.cpp`（面の Find Next）・`model/KFCSearchEngine.cpp`（種類の振り分け・状態行）・`model/KFCResultModel.cpp` の `BuildHitLocator`（行の種類の語）・`ui/KFCKohakuTab.*`（面）。
> 設計書 `docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md`（SP1〜SP6）。回帰の表＝`work/kbs-regress/sp-cases.tsv`（作る台本＝`work/note-scripts/2026-11-06-kfc-label-spelling/sp_cases.py`）。
> 作者の依頼（2026-11-06）「スペルミスの一覧：英文の波線の語　　検索タブにふやせるかな」→「あとは、おすすめで」。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 面

- **SPELL-01** ★**Kohaku+ の Find: に Spelling**。どの言語でも出る（Script Label と Story の間＝ABC 順・KOHAKU-03）。回帰 `kh-kind-order`・`sp-other-language`
  - 訂正:

- **SPELL-02** Search: は Table・Ruby・Kenten・Warichu と同じ5つ（All Documents／Document／Story／To End of Story／Selection）。今の選択で選べない範囲は文書全体を探し、そう言う。Book Scope も効く。回帰 `sp-scope-story`・`sp-scope-fallback`・`sp-alldocs`・`sp-book`
  - 訂正:

- **SPELL-03** 面＝欄は無く、Search の下のアイコンだけ（Kana・Width Sensitive は出さない＝文字を比べない・Warichu と同じ）。右の列は Find Next と Find in KFC だけ（変えるものが無い）。回帰 `sp-face-columns`
  - 訂正:

## 2. 探す

- **SPELL-10** ★**本体の自動スペルチェックの記録（`ISpellingDataStrand`）を読む＝一覧＝画面の波線**。種類は赤（スペルミス）と緑（繰り返した語・大文字で始まらない語・大文字で始まらない文）の4つ（作者「おすすめで」）。ノート・削除テキスト・隠した条件の文字は行にしない。語は、その文字の言語で調べられたもの（英語の辞書は `london` をスペルミスにする＝実測）。本体は開いている全文書（見えないページ・あふれ・親ページ・後ろの文書・窓の無い文書も）を手の空いたときに調べる（実測 `docs/ai-notes/kfc-label-spelling-measure-2026-11-06.md`）。回帰 `sp-find`・`sp-none`・`sp-overset`
  - 訂正:

- **SPELL-11** ★**自動スペルチェックが OFF なら探さずに状態行で言う**（`Spelling: Dynamic Spelling is off - turn it on (Edit > Spelling) and search again.`・設定は変えない）。面の Find Next も同じ（`Spelling: Dynamic Spelling is off - turn it on (Edit > Spelling).`）。回帰 `sp-off-switch`・`sp-off-pref`・`sp-face-off`
  - 訂正:

- **SPELL-12** ★**本体がまだ調べていない文字は行にならず、そのストーリーの数を状態行で言う**（0件のときも）＝開いている文書は `Dynamic Spelling has not checked N story(s) yet - search again in a moment.`（本体は手の空いたときに調べる）。★**ブックの閉じた章は保存したときの記録で読む**（記録は文書と一緒に保存される＝実測）＝確かめる前に保存された章は `N story(s) of the closed chapters had not been checked when they were saved - open the chapter, let InDesign check it, save it, and search again.`（KFC は章を開いてすぐ返す＝本体が調べる間が無い。本体の辞書で補う形は作らない＝設計書 SP5 の✅）。回帰 `sp-book`・`sp-book-unchecked`
  - 訂正:

- **SPELL-13** アイコン（ロックしたレイヤー・ロックしたストーリー・隠したレイヤー・親ページ・脚注）は本体のテキストタブと同じ働き。表のセル・脚注の中も探す。回帰 `sp-locked-off`・`sp-locked-on`・`sp-master-off`・`sp-master-on`・`sp-footnote`・`sp-cell`
  - 訂正:

- **SPELL-14** 段の名前＝`[Spelling/Document] Find:"[Spelling]"`（All Documents・ブックはそれぞれの語）。数の名詞＝`word(s)`（`4 word(s).`）。検索の後の文＝`Click a row to go to its word.`。回帰 `sp-find`
  - 訂正:

## 3. 行

- **SPELL-20** ★**行＝波線1本ごとの文字の行**（本体の記録の誤りの run＝語にぴったり・繰り返しは2語で1本＝`[the the]` で1行）。位置の語（locator）のページの後ろに種類＝`misspelled`／`repeated`／`uncapitalized`／`uncapitalized sentence`（本体の環境設定＞スペルチェックの4つの名前・行のほかの事実と同じ小文字の書き方＝`P1(2) repeated`）。回帰 `sp-find`
  - 訂正:

- **SPELL-21** 行のクリック＝その語を選ぶ（文字の行の決まりどおり）。回帰 `sp-click`
  - 訂正:

- **SPELL-22** Search This Document Again は語を並べ直す。回帰 `sp-doc-again`
  - 訂正:

## 4. 書かない

- **SPELL-30** ★**書かない**＝行の Replace・Return は灰色（扉の文 `Replace: Find: Spelling lists the words Dynamic Spelling underlines and changes nothing - a row is not replaced.`）。Change All in Book は灰色（扉 `Change All: the Find/Change dialog is on Spelling on the Kohaku+ tab. This panel lists the words Dynamic Spelling underlines - nothing to change.`）。面に Change 系は無い（扉 `Find: Spelling lists the words Dynamic Spelling underlines and changes nothing.`）。回帰 `sp-replace-grey`・`sp-changeall-book-grey`
  - 訂正:

- **SPELL-31** 文字の行の Special Operations（Clear Overrides・Create Outlines・Tate-chu-yoko・Run Script）は、ほかの Kohaku+ の文字の行（Warichu）と同じく効く。
  - 訂正:

## 5. 面のボタンとクエリ

- **SPELL-40** 面の **Find Next**＝範囲の次の波線の語へ行って選ぶ（最後の次は最初へ）。面の1行目＝`<語> - page <ページ>`。無ければ本体のアラート `Cannot find a match.`。回帰 `sp-face-next`・`sp-face-none`
  - 訂正:

- **SPELL-41** **Find in KFC**＝パネルの Find を Spelling で。回帰 `sp-findinkfc`
  - 訂正:

- **SPELL-42** クエリ＝`Spelling - <名前>.xml`（アイコンだけ）。どの言語でも出る。本体のクエリの欄で選ぶと Kohaku+ が Spelling で開く。Run Saved Queries の Find は一覧を作り、Run は書くものが無いので飛ばして言う（QR Code・Warichu・Story と同じ）。回帰 `sp-q-pick`・`sp-q-save-roundtrip`・`sp-q-find`・`sp-q-run`・`sp-q-menu-other-language`・`sp-q-dialog-other-language`
  - 訂正:

---

## ⬜ この章で未確認

- 本体が「大文字で始まらない語」（`uncapitalized`）を出す例（英語の辞書は `london` をスペルミスにした）。
- 実機の全件（`sp-cases` 30 件）は未回し（2026-11-06 時点＝試験を書いてビルド待ち）。
