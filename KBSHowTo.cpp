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
	L"[Searching (Find in Document / Find in Book)]\n"
	L"- What to look for is typed in Edit > Find/Change. This plug-in has no search box of its own.\n"
	L"- Searches on the Text, GREP and Glyph tabs.\n"
	L"- The Search: menu in Edit > Find/Change (Document, Story and so on) is not used. A run always covers the whole document (every chapter's whole document, for a book).\n"
	L"- The Object and Colour tabs are not supported.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON means every chapter of the active book.\n"
	L"- Closed chapters are opened invisibly to be read.\n"
	L"\n"
	L"[Find Missing Glyphs]\n"
	L"- Lists every place a font has no glyph for the character in front of it - what the official preflight profile calls Missing glyph.\n"
	L"- Missing glyphs inside overset text cannot be checked.\n"
	L"\n"
	L"[Find Overset]\n"
	L"- Lists every place text did not fit.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- The up and down arrow keys walk the whole tree, opening the rows they pass through.\n"
	L"- Hide Previous Chapter: with it ON, the other unmodified document windows are closed as a jump lands.\n"
	L"\n"
	L"[Replacing (Change Checked)]\n"
	L"- Tick the rows you want and run Change Checked from the menu: only the ticked occurrences are replaced.\n"
	L"- Check All / Uncheck All are on the result rows' right-click menu. Over the book row they mean every chapter, over a document row that chapter alone. They reach rows beyond the ones on screen.\n"
	L"- Nothing is saved for you: every chapter a replacement lands in is left open and unsaved, so save what you want to keep. Cancelling puts the whole run back.\n"
	L"- Replacing searches the chapter again and gives the Nth match the Nth ticked row's replacement. It does not verify that the match is still the one the search found, so if the text has been edited in between, a replacement can land somewhere you did not tick. Search again after editing.\n"
	L"\n"
	L"[Saving the results (Save Results...)]\n"
	L"- Writes what the panel is holding to a tab-separated text file, one line per hit.\n"
	L"\n"
	L"[Panel appearance]\n"
	L"- Translucent Panel: ON draws this panel faint while it floats, and brings it back to solid while the pointer is on it. It does nothing while the panel is docked and expanded. Windows only.\n"
	L"- Translucent Find/Change: the same for InDesign's own Find/Change dialog. Windows only.\n"
	L"- Both start OFF every time InDesign is launched. Save Panel Settings keeps them for the next launch.\n"
	L"\n"
	L"[Limits]\n"
	L"- The panel draws the first 5000 rows. Every hit is still held, so checking, replacing and saving are never capped by what is on screen.\n"
	L"- A run collects at most 10000 rows and says so when it stops there.\n"
	L"\n"
	L"[Please note]\n"
	L"- The search follows InDesign's own Find/Change settings. Formats and other options left in that dialog narrow this search as well.\n"
	L"\n"
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.";

const wchar_t* const kHowToJA =
	L"【検索（Find in Document / Find in Book）】\n"
	L"・検索する文字は 編集 > 検索と置換 に検索内容を入力します。このプラグインは検索入力欄を持ちません。\n"
	L"・Text / GREP / Glyph の3つのタブで検索できます。\n"
	L"・編集 > 検索と置換の Search:（Document / Story など）は使いません。常にドキュメント全体（ブックなら各章の全体）を検索します。\n"
	L"・オブジェクト／カラーの各タブは対象外です。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝アクティブなブックの全章。\n"
	L"・閉じている章は非表示で開いて調べます。\n"
	L"\n"
	L"【欠落グリフの検索（Find Missing Glyphs）】\n"
	L"・フォントにその文字の字形が無い箇所を一覧にします。公式のプリフライトの「欠落グリフ」と同じものです。\n"
	L"・あふれ（オーバーセット）に有る欠落グリフは調べられません。\n"
	L"\n"
	L"【あふれの検索（Find Overset）】\n"
	L"・入りきらなかったテキストを一覧にします。\n"
	L"\n"
	L"【結果の見方】\n"
	L"・上下の矢印キーで、途中の行を開きながらツリー全体を巡回できます。\n"
	L"・Hide Previous Chapter: ON にすると、ジャンプ後にジャンプ先以外の未変更のドキュメントウィンドウを閉じます。\n"
	L"\n"
	L"【置換（Change Checked）】\n"
	L"・検索結果の行にチェックを入れ、メニューの Change Checked を実行すると、チェックした箇所だけが置換されます。\n"
	L"・チェックの一括操作は、結果の行を右クリック > Check All / Uncheck All。ブック行なら全章、ドキュメント行ならその章だけに効きます。表示されていない分にも効きます。\n"
	L"・保存はしません。置換が入った章は開いたまま未保存で残るので、必要なものはご自身で保存してください。キャンセルすると実行全体が元に戻ります。\n"
	L"・置換は章をもう一度検索し、N 番目の一致に N 番目のチェック行の置換を充てます。「検索したときと同じ箇所か」は確認しないので、検索後にテキストを編集していると、チェックしていない場所が置換されることがあります。編集したら検索し直してください。\n"
	L"\n"
	L"【結果の書き出し（Save Results...）】\n"
	L"・いま出ている結果を、1ヒット1行のタブ区切りテキストで保存します。\n"
	L"\n"
	L"【パネルの見た目】\n"
	L"・Translucent Panel: ON にすると、パネルが浮いている（フロート）あいだ半透明になり、ポインタを乗せているあいだは元の濃さに戻ります。ドッキングして開いているときは効きません。Windows のみ。\n"
	L"・Translucent Find/Change: InDesign 本体の 検索と置換 ダイアログにも同じことをします。Windows のみ。\n"
	L"・どちらも InDesign を起動するたびに OFF から始まります。Save Panel Settings を実行しておくと、次回の起動でもその状態で開きます。\n"
	L"\n"
	L"【制限】\n"
	L"・パネルに出るのは先頭5000行までです（内部にはすべて保持しているので、チェックや置換、書き出しが表示数で制限されることはありません）。\n"
	L"・1回の実行で集めるのは10000件までです。そこで打ち切ったときはその旨をお知らせします。\n"
	L"\n"
	L"【注意】\n"
	L"・検索は InDesign 本体の 検索と置換 の設定に従います。書式の指定などが残っていると、この検索も同じように絞り込まれます。\n"
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
	//
	// It is the one route that needs a heading. The reference itself carries none - the ScriptUI
	// window puts "Kohaku Find/Change - How to Use" in its title bar, so a first line saying the same
	// thing was just repeating it (user's call, 2026-08-04) - but CAlert has no title of its own to
	// borrow: its bar says "Adobe InDesign", and the text would start mid-reference with nothing
	// naming what it belongs to. Same wording as the window title, in both languages, so the two
	// routes are recognisably the same document.
	PMString fallback;
	fallback.SetTranslatable(kFalse);
	fallback.Append("Kohaku Find/Change - How to Use\n\n");
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(text));
	CAlert::InformationAlert(fallback);
}

// End, KBSHowTo.cpp.
