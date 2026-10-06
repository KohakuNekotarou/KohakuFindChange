# 07. 変更履歴 ― 利用者の設定のまま

> 2026-10-06 夜の作者の決定（docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F1・F2）で、それまでの「署名・色・却下と承認」（TRK-01〜25）と Show Changes（第8章）は外した。番号は使い回さない。
> コード地図での位置: 置換は第6章（`model/KFCReplaceEngine.cpp`・`model/KFCChangeAll.cpp`・`model/KFCQuerySequence.cpp`）。KFC の中に変更履歴を扱うコードは無い。
> 訂正の書き方は [00-index.md](00-index.md)。

---

- **TRK-26** KFC は変更履歴に触らない ―― ストーリーの「変更をトラック」を ON にしない・戻さない、一致の周りの未承認の変更を承認しない、記録に名前や時刻を付けない。
  - 訂正:

- **TRK-27** 利用者が「変更をトラック」を ON にしているストーリーでは、置換（行の Replace・Change All in Book (No List)・クエリ連続実行）は InDesign 本体の検索と置換と同じく、利用者の名前で記録される。利用者の未承認の挿入の中の一致は、本体と同じくその挿入の中で書き換わる（削除の記録は付かない）。
  - 訂正:

- **TRK-28** 記録から戻す・承認する・一覧に戻す機能は KFC には無い（Reject Change／Accept Change／Accept All Changes by KohakuFindChange／Show Changes by KohakuFindChange／Replace Again は 2026-10-06 に外した）。戻すのは Ctrl+Z。記録の扱いは InDesign の変更履歴パネルか KCM。
  - 訂正:

- **TRK-29** それまでに KFC が `KohakuFindChange` の名前で付けた記録は、文書にそのまま残る。KFC はもう読まない。
  - 訂正:
