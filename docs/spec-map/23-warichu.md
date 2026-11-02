# 23. 割注を一覧にする（Kohaku+ タブの Find: Warichu）

> コード地図での位置: `model/KFCWarichuSearch.*`（歩く・行の字）・`model/KFCFaceText.cpp`（面の Find Next）・`ui/KFCKohakuTab.*`（面）。
> 設計書 `docs/superpowers/specs/2026-11-02-kfc-warichu-tcy-design.md`（A1〜A6）。回帰の表＝`work/kbs-regress/wr-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 面

- **WARI-01** ★**日本語で起動しているときだけ** Find: の5つ目に出る（作者 11-02「たてなかよこ、割り注の検索タブも欲しい。qrタブと同じ感じ、日本語限定」＝KOHAKU-03）。回帰 `wr-not-japanese`
  - 訂正:

- **WARI-02** ★**探すだけで書かない**（QR Code と同じ）＝面に Change／Change All／Change/Find は出ない。右の列は Find Next と Find in KFC だけで、Change 系のあった場所は空く（どの種類でもボタンの位置をそろえる＝KOHAKU-02）。欄は無い。回帰 `wr-face-columns`
  - 訂正:

- **WARI-03** ★**Search: の下のアイコン**＝本体のテキストタブの7つのうち、カナと全角半角を区別する2つは出さない（文字を比べないので効かない）。残りの5つ（ロックされたレイヤー・ロックされたストーリー・非表示のレイヤー・親ページ・脚注）は効き、Warichu の分は Ruby・Kenten と別に持つ（KENTEN-14）。回帰 `wr-face-columns`・`wr-locked-off`・`wr-locked-on`
  - 訂正:

- **WARI-04** Search: は文字の5つ（All Documents／Document／Story／To End of Story／Selection＝KOHAKU-10）。回帰 `wr-alldocs`
  - 訂正:

## 2. 探す

- **WARI-10** ★**割注の文字の続く範囲が1行**。段落の終わりをまたぐ割注は、その前と後で2行（圏点の行と同じ切り方＝KENTEN-10）。本文・表のセル・脚注・文字スタイルで付けた割注も出す。回帰 `wr-find`・`wr-two-runs`・`wr-para-split`・`wr-cell`・`wr-footnote`・`wr-style`
  - 訂正:

- **WARI-11** 隠した条件の中は出さない。ロックされたレイヤーの上の割注はアイコンに従う（本体と同じ）。ブックの閉じた章も開いて読み、返す。回帰 `wr-hidden`・`wr-locked-*`・`wr-book`
  - 訂正:

- **WARI-12** 段の名前＝`[Warichu/Document] Find:"[Warichu]"`。割注が無い＝0件の段。回帰 `wr-find`・`wr-none`
  - 訂正:

- **WARI-13** ★面の **Find Next**＝次の割注へ行って選び、面の下の行に字とページを言う（`割注一 - page 1`・最後の次は最初へ戻る）。1つも無ければ本体の `Cannot find a match.`。**Find in KFC**＝パネルの Find in … を Warichu で走らせる（KOHAKU-36）。回帰 `wr-face-next`・`wr-face-none`・`wr-findinkfc`
  - 訂正:

## 3. 行

- **WARI-20** 行は文字の行（一致の部分に `[割注の字]`）。Search This Document Again は同じ行を出す。回帰 `wr-find`・`wr-doc-again`
  - 訂正:

- **WARI-21** ★行の **Replace は効かない**＝灰色。Return も書かずに言う（`Replace: Find: Warichu lists the warichu and changes nothing - a row is not replaced.`）。パネルの Change All も書かずに言う（`Change All: the Find/Change dialog is on Warichu on the Kohaku+ tab. This panel lists the warichu there - nothing to change.`）。回帰 `wr-replace-grey`
  - 訂正:

- **WARI-22** ★行の **Special Operations ▸ Tate-chu-yoko** は効く（本文の一致＝REP-41）。回帰 `wr-tcy`
  - 訂正:

## 4. クエリ

- **WARI-30** ★保存するのは**アイコンだけ**（ファイル＝`Find-Change Queries\Warichu - <名前>.xml`＝KOHAKU-30）。本体のクエリの欄で選ぶ＝タブが Warichu で開き、アイコンが入る。回帰 `wr-q-pick`・`wr-q-save-roundtrip`
  - 訂正:

- **WARI-31** KFC のクエリの窓の Find＝そのアイコンで割注を一覧にする。★Run＝書き換えるものが無いので飛ばして、そう言う（QR Code と同じ）。回帰 `wr-q-find`・`wr-q-run`
  - 訂正:

- **WARI-32** 日本語でない InDesign＝本体の欄で選んでも読まずに言う（`Warichu - <名前> is for a Japanese InDesign - nothing was loaded.`）。KFC のクエリの窓には出ない。回帰 `wr-q-menu-not-japanese`・`wr-q-dialog-not-japanese`
  - 訂正:

---

## ⬜ この章で未確認

この章を書くにあたって**確かめていない**もの。「書いていない＝そういう決まりが無い」ではない。

- 本体の割注設定（行数・行間・大きさの比率など）は見ない＝割注が付いているかどうか（`kTAWarichuAttrBoss` の真偽）だけで探す。設定ごとに分けて探す口は作っていない。
- パネルの Change All（WARI-21 の2つ目の文）と、行の Return での断り（1つ目の文）は、コードで読んだだけで試験が無い（灰色の Replace は `wr-replace-grey`）。
- Search: の Story／To End of Story／Selection は割注では試験していない（作りは圏点と同じ＝範囲に触れていれば行＝KOHAKU-14・`KFCSearchEngine::StretchesTouch`）。
- 縦組みの文書の割注の行を、実機の絵では見ていない。
