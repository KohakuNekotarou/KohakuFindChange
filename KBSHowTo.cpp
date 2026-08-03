//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  "How to Use..." on the panel's flyout menu (2026-08-03): the plug-in's operating reference,
//  shown in a ScriptUI dialog run through the plug-in's own script engine - a multiline edit box,
//  so the text can be scrolled, selected and copied, none of which CAlert's static text can do.
//  The recipe (private engine name, pure-ASCII script built by escaping every non-ASCII code unit
//  as \uXXXX, CAlert as the fallback) is KESCL's, written for the same job.
//
//  Why the text is here and not in the string tables: odfrc caps a single string literal at about
//  3.1KB and this reference is several times that. KESCM's reference, which still sits in its
//  .fr, is already within 30 bytes of that ceiling and cannot be extended - so KBS starts where
//  KESCM will have to end up.
//
//  NOTE: this file holds Japanese text and MUST stay UTF-8 WITH BOM - without it MSVC reads it as
//  CP932 and the wide literals below break.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IExtendScriptUtils.h"		// RunScriptInEngine - the ScriptUI dialog

// General includes:
#include "CAlert.h"
#include "PMString.h"
#include "LocaleSetting.h"			// which language to show
#include "PMLocaleIds.h"			// k_jaJP

#include <string>
#include <cstdio>

// Project includes:
#include "KBSHowTo.h"

namespace
{

//========================================================================================
// The reference text, one wide literal per language
//========================================================================================

const wchar_t* const kHowToEN =
	L"Kohaku Find/Change - How to Use\n"
	L"\n"
	L"[Searching (Find in Document / Find in Book)]\n"
	L"- Searches on the Text, GREP and Glyph tabs. A Find Format can be set as well.\n"
	L"- What to look for is typed in Edit > Find/Change. This plug-in has no search box of its own.\n"
	L"- The tab, and the search options under Search: (master pages / locked layers / hidden layers / locked stories / footnotes / case sensitive / whole word), are used exactly as that dialog has them. Those settings are kept SEPARATELY PER TAB.\n"
	L"- The dialog's own Search: menu (Document, Story and so on) is NOT used. The scope is set by Book Scope below, and a run always covers the whole document (every chapter's whole document, for a book).\n"
	L"- The Object, Colour and transliterate tabs are not supported.\n"
	L"- Leave the find box empty and set only a Find Format to search by formatting alone.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: OFF (the default) means the front document, ON means every chapter of the active book. It applies to all three commands.\n"
	L"- Closed chapters are opened invisibly, read, and closed again as soon as that chapter is done. Chapters you had open yourself are never touched.\n"
	L"\n"
	L"[Find Missing Glyphs]\n"
	L"- Lists every place a font has no glyph for the character in front of it - what the official preflight profile calls Missing glyph.\n"
	L"- The results are grouped by font, because a box is fixed by changing the font.\n"
	L"- Overset text cannot be checked.\n"
	L"\n"
	L"[Find Overset]\n"
	L"- Lists every place text did not fit: a frame's red + and a table cell overflowing on its own, nested tables included.\n"
	L"- Clicking a row scrolls to the + itself.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- Clicking a row jumps to it, and a marker flashes over the spot for about a second (the text is not selected).\n"
	L"- The up and down arrow keys walk the whole tree, opening the rows they pass through.\n"
	L"- Hide Previous Chapter: with it ON, the other unmodified document windows are closed as a jump lands.\n"
	L"\n"
	L"[Replacing (Change Checked)]\n"
	L"- Tick the rows you want and run Change Checked from the menu: only the ticked occurrences are replaced.\n"
	L"- What they are replaced with is the Change to box in Edit > Find/Change. A Change Format works too - leave the change box empty and set only a Change Format to change the formatting and leave the text alone.\n"
	L"- Check All / Uncheck All are on the result rows' right-click menu. Over the book row they mean every chapter, over a document row that chapter alone. They reach rows beyond the ones on screen.\n"
	L"- With save after replace OFF (the default): the chapters are left open and UNSAVED, and the whole run is a single undo step.\n"
	L"- With save after replace ON (the box in the confirmation): each chapter is opened, replaced, saved and closed in turn, so a twenty-chapter book still holds only one chapter at a time.\n"
	L"- Cancelling a saving run: chapters already saved cannot be taken back, so it stops there. What was done stays in the results, and the chapters it never reached are marked cancelled.\n"
	L"\n"
	L"[Saving the results (Save Results...)]\n"
	L"- Writes what the panel is holding to a tab-separated text file, one line per hit.\n"
	L"\n"
	L"[Limits]\n"
	L"- The panel draws the first 5000 rows. Every hit is still held, so checking, replacing and saving are never capped by what is on screen.\n"
	L"- A run collects at most 10000 rows and says so when it stops there.\n"
	L"- Do not switch Find/Change tabs between a search and Change Checked (KBS notices and refuses the run).\n"
	L"\n"
	L"[Please note]\n"
	L"- The search follows InDesign's own Find/Change settings. Case sensitivity, whole word and any formats left in that dialog narrow this search as well.\n"
	L"\n"
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.";

const wchar_t* const kHowToJA =
	L"Kohaku Find/Change の使い方\n"
	L"\n"
	L"【検索（Find in Document / Find in Book）】\n"
	L"・Text / GREP / Glyph の3つのタブで検索できます。Find Format（検索形式）の指定も使えます。\n"
	L"・検索する文字は 編集 > 検索と置換 に入力します。このプラグインは検索欄を持ちません。\n"
	L"・タブと、Search: の下の検索オプション（マスターページ／ロックされたレイヤー／非表示レイヤー／ロックされたストーリー／脚注／大文字小文字の区別／完全一致）は、そのダイアログの設定がそのまま使われます。これらの設定はタブごとに別々です。\n"
	L"・ダイアログの Search:（Document / Story など）は使いません。対象範囲は下の Book Scope で決まり、常にドキュメント全体（ブックなら各章の全体）を検索します。\n"
	L"・オブジェクト／カラー／文字種変換の各タブは対象外です。\n"
	L"・検索文字を空にして Find Format だけを指定すると、その書式の箇所を探します。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: OFF（既定）＝前面のドキュメント、ON＝アクティブなブックの全章。下の3つのコマンドすべてに効きます。\n"
	L"・閉じている章は非表示で開いて調べ、その章が終わり次第すぐ閉じます。自分で開いていた章には触りません。\n"
	L"\n"
	L"【欠落グリフの検索（Find Missing Glyphs）】\n"
	L"・フォントにその文字の字形が無い箇所を一覧にします。公式のプリフライトの「欠落グリフ」と同じものです。\n"
	L"・結果はフォントごとにまとまります（□はフォント単位で直すため）。\n"
	L"・あふれ（オーバーセット）のテキストは調べられません。\n"
	L"\n"
	L"【あふれの検索（Find Overset）】\n"
	L"・入りきらなかったテキストを一覧にします。テキストフレームの「+」に加え、表のセル単独のあふれ（入れ子の表を含む）も検出します。\n"
	L"・行をクリックすると「+」の位置へスクロールします。\n"
	L"\n"
	L"【結果の見方】\n"
	L"・行をクリックするとその箇所へジャンプし、マーカーが1秒ほど点滅します（テキストは選択されません）。\n"
	L"・上下の矢印キーで、途中の行を開きながらツリー全体を巡回できます。\n"
	L"・Hide Previous Chapter: ON にすると、ジャンプ後にジャンプ先以外の未変更のドキュメントウィンドウを閉じます。\n"
	L"\n"
	L"【置換（Change Checked）】\n"
	L"・検索結果の行にチェックを入れ、メニューの Change Checked を実行すると、チェックした箇所だけが置換されます。\n"
	L"・置換後の文字は 編集 > 検索と置換 の「置換文字列」です。Change Format（置換形式）も使えます。置換文字を空にして Change Format だけを指定すると、文字はそのままで書式だけが変わります。\n"
	L"・チェックの一括操作は、結果の行を右クリック > Check All / Uncheck All。ブック行なら全章、ドキュメント行ならその章だけに効きます。表示されていない分にも効きます。\n"
	L"・置換後保存が OFF のとき（既定）：章は開いたまま・未保存で残り、実行全体が取り消し1回分です。\n"
	L"・置換後保存が ON のとき（確認ダイアログのチェックボックス）：1章ずつ「開く→置換→保存→閉じる」を繰り返すので、20章のブックでも一度に抱える章は1つです。\n"
	L"・ON でキャンセルしたとき：保存済みの章は取り消せないのでそこで停止します。済んだ分は結果に残り、届かなかった章には cancelled と出ます。\n"
	L"\n"
	L"【結果の書き出し（Save Results...）】\n"
	L"・いま出ている結果を、1ヒット1行のタブ区切りテキストで保存します。\n"
	L"\n"
	L"【制限】\n"
	L"・パネルに出るのは先頭5000行までです（内部にはすべて保持しているので、チェックや置換、書き出しが表示数で制限されることはありません）。\n"
	L"・1回の実行で集めるのは10000件までです。そこで打ち切ったときはその旨をお知らせします。\n"
	L"・検索してから Change Checked を実行するまでの間に、検索と置換のタブを切り替えないでください（KBS が検知して実行を断ります）。\n"
	L"\n"
	L"【注意】\n"
	L"・検索は InDesign 本体の 検索と置換 の設定に従います。大文字小文字／完全一致／書式の指定がそのダイアログに残っていると、この検索も同じように絞り込まれます。\n"
	L"\n"
	L"【免責】 どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。";

//========================================================================================
// Helpers
//========================================================================================

/** Append 'text' to 'out' as the body of a JavaScript double-quoted string literal.
	Everything outside printable ASCII goes out as \uXXXX, so the generated script is pure
	ASCII whatever the text held - no encoding to get wrong on the way to the engine. */
void AppendJSEscaped(std::string& out, const wchar_t* text)
{
	for (const wchar_t* p = text; *p != 0; ++p)
	{
		const wchar_t ch = *p;
		switch (ch)
		{
			case '"':	out += "\\\"";	break;
			case '\\':	out += "\\\\";	break;
			case '\n':	out += "\\n";	break;
			case '\r':	out += "\\r";	break;
			case '\t':	out += "\\t";	break;
			default:
				if (ch >= 0x20 && ch <= 0x7E)
				{
					out += static_cast<char>(ch);
				}
				else
				{
					char esc[8];
					std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(ch));
					out += esc;
				}
				break;
		}
	}
}

} // anonymous namespace

//========================================================================================
// KBSHowTo::Show
//========================================================================================

void KBSHowTo::Show()
{
	// Japanese InDesign gets the Japanese reference, everything else the English one - the split
	// the string tables make for the replace prompts.
	const wchar_t* const text =
		(LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP) ? kHowToJA : kHowToEN;

	Utils<IExtendScriptUtils> esUtils;
	if (esUtils.Exists())
	{
		// The box is editable on purpose: an editable field is guaranteed selectable, and any edit
		// dies with the dialog. A private engine name keeps this out of the user's own
		// #targetengine sessions.
		std::string js;
		js.reserve(16384);
		js += "(function () {\n"
			  "  var w = new Window(\"dialog\", \"Kohaku Find/Change - How to Use\");\n"
			  "  var t = w.add(\"edittext\", undefined, \"";
		AppendJSEscaped(js, text);
		js += "\", {multiline: true, scrolling: true});\n"
			  "  t.preferredSize = [640, 520];\n"
			  "  var g = w.add(\"group\");\n"
			  "  g.alignment = \"right\";\n"
			  "  g.add(\"button\", undefined, \"OK\", {name: \"ok\"});\n"
			  "  w.show();\n"
			  "}());";

		PMString scriptText(js.c_str());
		scriptText.SetTranslatable(kFalse);
		if (esUtils->RunScriptInEngine("KBS", scriptText, kFalse /*showErrorAlert*/, kFalse) == kSuccess)
			return;
	}

	// Script route unavailable or failed: the plain (non-scrollable) alert is better than no
	// reference at all.
	PMString fallback;
	fallback.SetTranslatable(kFalse);
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(text));
	CAlert::InformationAlert(fallback);
}

// End, KBSHowTo.cpp.
