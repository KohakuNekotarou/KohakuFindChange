# Kohaku Find/Change
Adobe InDesign C++ SDK Plug-In.
Searches the front document, or every chapter of the active book (.indb) at once.

## Adobe Exchange
https://exchange.adobe.com/apps/cc/205698/kohakufindchange

## Official binaries

The only official builds are the ones distributed through **Adobe Exchange** above.
Anything built from this source tree is an **unofficial build**. Please do **not**
report a problem with an unofficial build to Adobe — use the Discussions board
below, or contact the author.

Below are the SHA-256 hashes of the official `.pln` file, so that any build can
be checked against the released one. Listed from the next release onward; a
published row is never removed.

| Version | File | SHA-256 |
|---------|------|---------|
| 1.2.0 | KohakuFindChange.pln | `49ed476b84ce496572888a7abfc2ea74d7739254e9f744719d895956950e7d4d` |
| 1.2.0 | KohakuFindChangeUI.pln | `63e26563546682660977e6eaef67d69332354b7f709fd4aeb6576570195621f4` |
| 1.3.0 | KohakuFindChange.pln | `dac995dabbdb34f21cb4ad7c6d5c791e1a2ea8562b39c818a7c2b6caabebfb2a` |
| 1.3.0 | KohakuFindChangeUI.pln | `e440d3a08949e3c5f249ecfbf24d830bc53d39addcb18ec9164b50b8181bc5e2` |
| 2.1.0 | KohakuFindChange.pln | `d66240029a8ed66cbc4c17e2e3ac4c0bd8e558adad6a2e561708b488e609d793` |
| 2.1.0 | KohakuFindChangeUI.pln | `864c17d8ccc8661aa1e72787bd948b90ec34a8b2173e6d696c0d0784ca161c3b` |

Verify / 照合方法 (PowerShell):

```powershell
Get-FileHash -Algorithm SHA256 KohakuFindChange.pln, KohakuFindChangeUI.pln
```

## Sponsors
https://github.com/sponsors/KohakuNekotarou

## Discussions
https://github.com/KohakuNekotarou/KohakuFindChange/discussions

## About Creation
This plugin was designed and implemented by **KohakuNekotarou** in collaboration with Anthropic's AI, **Claude (Claude Code)**.

## License
This source is published to show what can be built with the Adobe InDesign C++
SDK. See [LICENSE](LICENSE) for the terms. In short: **read it, learn from it, and
reuse parts of it in your own plug-ins** — that is what this repository is for.
Please do not redistribute builds of *this* plug-in; a derivative you do
distribute has to carry its own name and its own ID prefix.

## Note
We cannot be held responsible for any issues that may arise; please use this service at your own risk.

## 連絡
農作業の合間に趣味でInDesignのScriptやPlugInを作っています。
お仕事のお依頼は
kohaku.nekotarou@gmail.com
まで、日本国内のみ対応します。
