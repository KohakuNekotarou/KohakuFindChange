# Kohaku Find/Change
Adobe InDesign C++ SDK Plug-In.
Searches the front document, or every chapter of the active book (.indb) at once.

## Adobe Exchange
https://exchange.adobe.com/apps/cc/205698/kohakufindchange

## Official binaries / 公式バイナリ

The only official builds are the ones distributed through **Adobe Exchange** above.
Anything built from this source tree is an **unofficial build**. Please do **not**
report a problem with an unofficial build to Adobe — use the Discussions board
below, or contact the author.

公式ビルドは上の **Adobe Exchange** で配布しているものだけです。
このソースからビルドされたものはすべて**非公式ビルド**です。
非公式ビルドの不具合を **Adobe へ報告しないでください**。
下の掲示板か作者までお願いします。

Below are the SHA-256 hashes of the official `.pln` file, so that any build can
be checked against the released one. Listed from the next release onward; a
published row is never removed.

以下は公式 `.pln` の SHA-256 です。手元のビルドが公開版と同一かを照合できます。
次のリリース以降を掲載します。一度載せた行は削除しません。

| Version | File | SHA-256 |
|---------|------|---------|
| _next release — to be added_ | | |

Verify / 照合方法 (PowerShell):

```powershell
Get-FileHash -Algorithm SHA256 KohakuFindChange.pln
```

## Sponsors 支援
https://github.com/sponsors/KohakuNekotarou

## Discussions 掲示板
https://github.com/KohakuNekotarou/KohakuChangeMarker/discussions

## About Creation
This plugin was designed and implemented by **KohakuNekotarou** in collaboration with Anthropic's AI, **Claude (Claude Code)**.

## License / ライセンス

This source is published to show what can be built with the Adobe InDesign C++
SDK. See [LICENSE](LICENSE) for the terms. In short: **read it, learn from it, and
reuse parts of it in your own plug-ins** — that is what this repository is for.
Please do not redistribute builds of *this* plug-in; a derivative you do
distribute has to carry its own name and its own ID prefix.

このソースは、Adobe InDesign C++ SDK で何が作れるのかを知ってもらうために公開しています。
条件は [LICENSE](LICENSE) を参照してください。要約すると、**読む・学ぶ・一部を自分の
プラグインに取り込む**のはご自由にどうぞ（この repo はそのために公開しています）。
**このプラグイン自体のビルドを再配布すること**はご遠慮ください。派生物を配布される場合は、
名前と ID プレフィックスをご自分のものに変えてください。

## Note
We cannot be held responsible for any issues that may arise; please use this service at your own risk.

## 連絡
農作業の合間に趣味でInDesignのScriptやPlugInを作っています。
お仕事のお依頼は
kohaku.nekotarou@gmail.com
まで、日本国内のみ対応します。
