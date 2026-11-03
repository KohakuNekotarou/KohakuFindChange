# 22. 圏点で探す・圏点を付ける（Kohaku+ タブの Find: Kenten）

> コード地図での位置: `model/KFCKentenSearch.*`（歩く・行の字・書く）・`model/KFCReplaceEngine.cpp` の `KentenWrite`（行の Replace）・`ui/KFCKohakuTab.*`（面）。
> 設計書 `docs/superpowers/specs/2026-10-31-kfc-kohaku-tab-design.md`（K20〜K23）・`docs/superpowers/specs/2026-11-03-kfc-kenten-face-japanese-design.md`（J・N・C・A・R＝11-03 の作り直し）。回帰の表＝`work/kbs-regress/kt-cases.tsv`。
> 訂正の書き方は [00-index.md](00-index.md)。

---

## 1. 面

- **KENTEN-01** 日本語で起動しているときだけ Find: に出る（KOHAKU-03）。部品＝**Base Text:**・**Kenten:**・**Change Kenten To:**、2つの一覧の右に**□**（KENTEN-04）。★3つの欄は左端と幅がそろう（ラベル115・欄は左から120・幅170＝作者 11-03「縦にそろっていないので揃えて欲しい」・A2）。値は KFC が覚える（起動している間だけ）。回帰 `kt-face-*`・`kt-face-columns`
  - 訂正: 11-03＝Look For:（Text With Kenten／Text Without Kenten）と Remove Kenten を無くした（作者「LookFor いるかな？　圏点の種類に None をいれるとよいような？それをつかうと、圏点をなくすほうにもつかえるような」＝一覧の「なし」に入れた・KENTEN-11・KENTEN-30）。それまでは Base Text の欄が幅215・「Change Kenten To:」の行だけ一覧が5px右だった（測った）。

- **KENTEN-02** ★**一覧の中身は本体の圏点設定の窓と同じ言葉・同じ順**（作者 11-03「圏点は日本語なので日本語表記に」・J1〜J3）＝`Kenten:` は **[すべての圏点]／なし／ゴマ／白ゴマ／蛇の目／黒丸／小さい黒丸／二重丸／黒三角／白三角／白丸／小さい白丸／カスタム**、`Change Kenten To:` は **[変更しない]**／なし／（同じ）／カスタム。種類の名前は本体のキー（`None`・`Black sesame`…`Custom`）を借りて本体の表で訳す。先頭の2つは KFC の言葉。ラベルは英語のまま。回帰 `kt-face-entries`
  - 訂正: 11-02〜11-03 は一覧の右に選んだ種類の印の見本があった（作者 11-03「横にサンプルをだすのはやめる」で外した）。種類の名前は英語（Sesame Dot ほか）だった。

- **KENTEN-04** ★**□（カスタムの字・1文字）**（作者 11-03「カスタムの中身を入れるための1文字だけ入力かのうなマス」・C1〜C3）＝その一覧がカスタムのときだけ使える（他では灰色・字は覚えておく）。いつも1文字で、新しく打った字・貼った字に置き換わる（前の字を除いた最初の字・空白は字に数えない・日本語入力の確定は次の idle で落ち着かせる）。本体のカスタム圏点は16ビットの1字＝**U+FFFF を超える字（𠮟・絵文字）は断る**（□を空にして面の1行目で言う）。異体字セレクタは落とす。回帰 `kt-face-char-grey`・`kt-face-char-type`・`kt-face-char-wide`
  - 訂正:

- **KENTEN-03** ★**How to Use...**（作者 11-02「圏点のほうにもHowToを」）＝欄の説明の窓（`KFCHowTo::ShowKenten`）。日本語だけ。★場所＝**面の右の列の Find in KFC の下**（180＝作者 11-02 夕「他のタブと同じように」＝KOHAKU-02）。Ruby の How to Use... とは別の部品。回帰 `kt-howto`・`kt-face-columns`・`rb-howto-place`
  - 訂正: 11-02 昼は本体の枠の行の完了の左（夕に右の列へ）。

## 2. 探す

- **KENTEN-10** ★**圏点の付いた文字**（Kenten: が [すべての圏点]・種類・カスタム）＝同じ種類の圏点の連なりが1行（文字スタイルの圏点も）。段落の終わり・本体の制御文字で切る。★カスタムは字が変わる所でも切る（字の違うカスタム圏点が隣り合えば2行・C6）。Base Text は含むか（そのまま比べる）・Kenten: は種類。（K20）回帰 `kt-any`・`kt-mark`・`kt-base`・`kt-both`・`kt-style`・`kt-two-paragraphs`・`kt-custom-split`
  - 訂正: 11-03 まではこの形を Look For: の Text With Kenten と呼んでいた。

- **KENTEN-11** ★**Kenten: なし**＝Base Text のうち圏点が無い箇所。Base Text が空なら断る（`Kenten: none looks for a text with no marks - type it in Base Text...`）。回帰 `kt-without`・`kt-without-nobase`・`kt-face-without`
  - 訂正: 11-03 までは Look For: の Text Without Kenten（作者が一覧の「なし」に入れた＝11-02 の「Text Without Kenten も今のまま」を作者が置き換えた）。

- **KENTEN-16** ★**Kenten: カスタム**（C4）＝□に字があればその字のカスタム圏点だけ、□が空ならどのカスタム圏点も。字は本体が持つ16ビットの字（`kTAKentenCharacterBoss`）をそのまま比べる（カナ・全角半角のアイコンは効かない）。回帰 `kt-custom-any`・`kt-custom-char`・`kt-face-custom-find`
  - 訂正:

- **KENTEN-12** 隠した条件の中は出さない。本文・セル・脚注の物語、★親ページ・ロック・非表示はアイコンの範囲（KENTEN-14＝11-02 に「全部の物語」から変えた）。上限 1000。回帰 `kt-hidden`・`kt-cell`
  - 訂正:

- **KENTEN-14** ★**Search: の下のアイコン**（作者 11-02）＝RUBY-15 と同じ7つ・Kenten の分は Ruby と別に持つ。回帰 `kt-scope-*`・`rb-switch-face`
  - 訂正:

- **KENTEN-15** ★カナ・全角半角を区別しないとき＝RUBY-16 と同じ（Base Text）。回帰 `kt-fold-*`
  - 訂正:

- **KENTEN-13** 段の名前＝`[Kenten/Document] Find:"[Any Kenten]"`（カスタムで□に字＝`Find:"《★》"`・□が空＝`《✱》`）。保存したクエリの Find はタブを譲る。回帰 `kt-yield-closed`・`kt-custom-any`・`kt-custom-char`
  - 訂正:

## 3. 行

- **KENTEN-20** ★行は文字の行。一致の部分に `親文字《印》`（`﹅﹆◉●•◎▲△○◦`、★カスタムは**その字**＝11-03・字が読めないときだけ `✱`）。着地・ページ・ロック・編集に付いていく・Copy・Clear はふつうの文字の行のまま。（K21）回帰 `kt-click`・`kt-next`・`kt-copy`・`kt-clear`・`kt-edit-moved`・`kt-locked`・`kt-custom-any`
  - 訂正: 11-02 まではカスタムはいつも `✱`。

- **KENTEN-21** ★**行は2行で描く**（作者 11-01「KCM の様に」・RUBY-21 と同じ作り）＝下の段に親文字、上の段に文字ごとの印＝字でなく図形（`ui/KFCKentenMark.*`＝`KFCMarkPassView`・AGM で字より先に）。★**形と大きさの割合は本体の圏点と同じ**（作者 11-03「パネルでの圏点の表示　画面を取って、実際のと比べてより似るようにしてほしい」＝本体の紙面を 600dpi で測った＝丸 0.69K・小さい黒丸 0.43K・小さい白丸 0.47K・蛇の目の中の点と二重丸の内側 0.38K・三角 0.76K×0.66K・ゴマ 0.65K×0.67K の曲がった粒・線 0.06K、印の中心は一本の線の上、丸の下端と字の頭の間 0.24K。K＝圏点の em＝本体は親文字の半分、**パネルは字幅の0.8**＝本体の1.6倍：本体と同じ0.5では蛇の目・二重丸・黒丸が同じ点になる）。線は最低1px・輪と内側の間も最低1px。カスタムはその字を圏点の em の大きさで書く。圏点を消した行は上の段に棒。⚠部品の文字と Copy は `親文字《印》` のまま。（F10〜F13）回帰 `kt-two-*`（見比べた絵の作り方＝`work/note-scripts/2026-11-03-kfc-kenten-face/look.ps1`・`compare_rows.py`）
  - 訂正: 11-02 までは KCM の図形のまま（字幅の約0.8の丸・太い線・まっすぐな涙形のゴマ・上の段の真ん中）。

## 4. Replace

- **KENTEN-30** ★行の右クリックの Replace と Return＝**その時の面の Change 側**。Change Kenten To＝その種類。書くのは種類の属性だけ＝見た目は触らない。★**カスタム**＝□の字のカスタム圏点（種類・字・文字の入力方法 0＝直接入力を1つのコマンドで＝C5）。★**なし**＝**圏点の属性を全部外す**（19個＝`ClearOverridesCmd`・サイズ・フォント・色なども本文に残さない＝作者 10-31「圏点の方もしっかり消せるか」）。文字スタイル・段落スタイルの圏点は、その文字の上で種類を「なし」にする（スタイルは他の文字のもの）。一緒に Ctrl+Z 1回で戻る。（K22）回帰 `kt-change-one`・`kt-change-all`・`kt-without-apply`・`kt-remove`・`kt-remove-wins`・`kt-remove-style`・`kt-remove-all`・`kt-remove-style-all`・`kt-remove-parastyle`・`kt-undo-remove-all`・`kt-undo-remove-style`・`kt-change-keeps-look`・`kt-return`・`kt-cell-change`・`kt-custom-change`・`kt-custom-other`・`kt-face-custom-change`・`kt-face-remove`
  - 訂正: 11-03 まではカスタムが無く、外すのは Remove Kenten のチェックボックス（Change Kenten To より優先）だった。

- **KENTEN-31** 断り＝Change 側に何も無い（[変更しない]）／★カスタムで□が空（`Replace: Change Kenten To asks for a custom mark with no character - type one in the box beside it...`）／既にそのとおり（カスタムは字まで同じとき）。回帰 `kt-nothing`・`kt-same`・`kt-custom-nochar`・`kt-custom-same`
  - 訂正:

- **KENTEN-32** ★全部の行で **Ctrl+Z 1回**（段の名前 `Change Kenten`）。行は `Changed`＋書いた後の字。回帰 `kt-undo`
  - 訂正:

- **KENTEN-41** ★**面のボタン**（作者 11-02「FindNextなどのぼたん Ruby、圏点の方にもつけれるかな？」）＝Find Next・Change・Change All・Change/Find が Table と同じ働き（TBL-60〜65）。Find Next＝Search: の範囲の圏点の行を一覧と同じ順に、選択から先へ（着地＝行のクリックと同じ）。1行目＝`<字>《<印>》 - page <ページ>`。Change＝見つけた箇所に Change Kenten To（なし＝外す）・Change All＝範囲の全部（どれも Ctrl+Z 1回・行の Replace と同じ書き方＝`KFCReplaceEngine::WriteFacePlaces`）。★カスタムで□が空なら Change・Change All・Change/Find は灰色（書くものが無い・C5）。アラートは本体の文（`Search is completed. N replacement(s) made.`／`Cannot find a match.`）。文書が開いていなければ1行目で言う。（B1〜B5）回帰 `kt-btn-next`・`kt-btn-changeall`・`kt-btn-nodoc`・`kt-btn-ja`・`kt-face-custom-nochar`
  - 訂正: 11-03＝右の列の並びが変わった（Find in KFC が一番上・KOHAKU-02）。

## 5. 効かない操作

- **KENTEN-40** 灰色と扉は Ruby と同じ（RUBY-40）。動く＝Copy・Clear・Search This Document Again・Search These Results Again。Change All in Book は灰色。回帰 `kt-menus`・`kt-search-again`・`kt-doc-again`・`kt-tier-back`・`kt-changeall-refused`
  - 訂正:
