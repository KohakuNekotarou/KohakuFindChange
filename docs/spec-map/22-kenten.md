# 22. 圏点で探す・圏点を付ける（Kohaku+ タブの Find: Kenten）

> コード地図での位置: `model/KFCKentenSearch.*`（歩く・行の字・書く）・`model/KFCReplaceEngine.cpp` の `KentenWrite`（行の Replace）・`ui/KFCKohakuTab.*`（面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-kohaku-tab-design.md`（K20〜K23）。回帰の表＝`work/kbs-regress/kt-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 面

- **KENTEN-01** 日本語で起動しているときだけ Find: に出る（KOHAKU-03）。部品＝**Look For:**（Text With Kenten／Text Without Kenten）・**Base Text:**・**Kenten:**（`[Any Kenten]`＋11種・Without のとき灰色）・**Change Kenten To:**（`[Leave As It Is]`＋10種）・**Remove Kenten**・案内の2行。値は KFC が覚える（起動している間だけ）。回帰 `kt-face-*`
  - 訂正:

## 2. 探す

- **KENTEN-10** ★**Text With Kenten**＝同じ種類の圏点の連なりが1行（文字スタイルの圏点も）。段落の終わり・本体の制御文字で切る。Base Text は含むか（そのまま比べる）・Kenten: は種類。（K20）回帰 `kt-any`・`kt-mark`・`kt-base`・`kt-both`・`kt-style`・`kt-two-paragraphs`
  - 訂正:

- **KENTEN-11** ★**Text Without Kenten**＝Base Text のうち圏点が無い箇所。Base Text が空なら断る。回帰 `kt-without`・`kt-without-nobase`
  - 訂正:

- **KENTEN-12** 隠した条件の中は出さない。本文・セル・マスターの全部の物語。上限 1000。回帰 `kt-hidden`・`kt-cell`
  - 訂正:

- **KENTEN-13** 段の名前＝`[Kenten/Document] F:"[Any Kenten]"`。保存したクエリの Find はタブを譲る。回帰 `kt-yield-closed`
  - 訂正:

## 3. 行

- **KENTEN-20** ★行は文字の行。一致の部分に `親文字《印》`（`﹅﹆◉●•◎▲△○◦`、Custom は `✱`）。着地・ページ・ロック・編集に付いていく・Copy・Clear はふつうの文字の行のまま。（K21）回帰 `kt-click`・`kt-next`・`kt-copy`・`kt-clear`・`kt-edit-moved`・`kt-locked`
  - 訂正:

## 4. Replace

- **KENTEN-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**。Change Kenten To＝その種類（Custom は無い）。書くのは種類の属性だけ＝見た目は触らない。Remove Kenten＝★**圏点の属性を全部外す**（19個＝`ClearOverridesCmd`・Change Kenten To より優先・サイズ・フォント・色なども本文に残さない＝作者 10-31「圏点の方もしっかり消せるか」）。文字スタイル・段落スタイルの圏点は、その文字の上で種類を「なし」にする（スタイルは他の文字のもの）。一緒に Ctrl+Z 1回で戻る。（K22）回帰 `kt-change-one`・`kt-change-all`・`kt-without-apply`・`kt-remove`・`kt-remove-wins`・`kt-remove-style`・`kt-remove-all`・`kt-remove-style-all`・`kt-remove-parastyle`・`kt-undo-remove-all`・`kt-undo-remove-style`・`kt-change-keeps-look`・`kt-return`・`kt-cell-change`
  - 訂正:

- **KENTEN-31** 断り＝Change 側に何も無い／既にそのとおり。回帰 `kt-nothing`・`kt-same`
  - 訂正:

- **KENTEN-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Change Kenten`）。行は `Changed`＋書いた後の字。回帰 `kt-undo`
  - 訂正:

## 5. 効かない操作

- **KENTEN-40** 灰色と扉は Ruby と同じ（RUBY-40）。動く＝Copy・Clear・Search This Document Again・Search These Results Again。Change All in Book は灰色。回帰 `kt-menus`・`kt-search-again`・`kt-doc-again`・`kt-tier-back`・`kt-changeall-refused`
  - 訂正:
