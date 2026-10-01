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
//  3.1KB and this reference is several times that - the reason KESCL moved its own reference into
//  C++ as well.
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

#include <string>
#include <cstdio>

// Project includes:
#include "KBSHowTo.h"
#include "KBSLoc.h"					// JapaneseUI - the one place "is the UI Japanese" is asked

namespace
{

//========================================================================================
// The reference text, one wide literal per language
//========================================================================================

const wchar_t* const kHowToEN =
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.\n"
	L"\n"
	L"[Searching (Find in Document / Find in Book)]\n"
	L"- Follows InDesign's own Find/Change settings.\n"
	L"- Searches on the four tabs Text, GREP, Glyph and Transliterate.\n"
	L"- The Object and Colour tabs are not supported.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON = every document of the book the Book panel is showing (the book whose tab is in front). If no Book panel can be found, the active book is used.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- A double-click selects the matched text.\n"
	L"- The up and down arrow keys walk the whole tree, opening the rows they pass through.\n"
	L"- Hide Previous Chapter: ON closes, after a jump, the document windows other than the one jumped to that have nothing to save. A document that has been edited, or has never been saved, is left open.\n"
	L"\n"
	L"[Replacing (Change Checked)]\n"
	L"- Follows InDesign's own Find/Change replace settings.\n"
	L"- Each replacement is recorded in Track Changes, with \"KohakuFindChange\" as its author.\n"
	L"- The result, story and document rows have a right-click menu.\n"
	L"- Inside a footnote Track Changes records nothing, so no menu is shown there.\n"
	L"- Replacements side by side are merged into one tracked change, so taking them back takes them back together.\n"
	L"- Before replacing, each story is searched again to check that every ticked match is still where it was found. If one has moved, the replace is stopped. Search again.\n"
	L"\n"
	L"[Listing this plug-in's changes again (Show Changes by KohakuFindChange)]\n"
	L"- Show Changes by KohakuFindChange on the menu lists those changes again. Book Scope decides whether it reads the front document or every chapter of the book.\n"
	L"\n"
	L"[The Book panel (Remember Book Panel Placement)]\n"
	L"- ON: InDesign's own Book panel opens where it was and at the size it was (collapsed to icons, if it was) when it was last closed. This is recorded when a book is closed and when InDesign quits.";

const wchar_t* const kHowToJA =
	L"【免責】 どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。\n"
	L"\n"
	L"【検索（Find in Document / Find in Book）】\n"
	L"・InDesign本体の検索の設定に従います。\n"
	L"・テキスト / 正規表現 / 字形 / 文字種変換 の4つのタブで検索できます。\n"
	L"・オブジェクト／カラーのタブは対象外です。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝ブックパネルに表示しているブック（タブが手前にあるブック）の全ドキュメント。ブックパネルが見つからないときは、アクティブなブックを対象にします。\n"
	L"\n"
	L"【結果の見方】\n"
	L"・ダブルクリックすると、一致した文字列を選択します。\n"
	L"・上下の矢印キーで、途中の行を開きながらツリー全体を巡回できます。\n"
	L"・Hide Previous Chapter: ON にすると、ジャンプ後にジャンプ先以外の、保存するものが無いドキュメントウィンドウを閉じます。編集されたもの、一度も保存していないものは、そのまま残します。\n"
	L"\n"
	L"【置換（Change Checked）】\n"
	L"・InDesign本体の置換の設定に従います。\n"
	L"・置換は変更履歴に記録され、作成者は「KohakuFindChange」になります。\n"
	L"・結果、Story、ドキュメントの行では右クリックでメニューが表示されます。\n"
	L"・脚注の中は変更履歴が記録されないためメニューが表示されません。\n"
	L"・隣り合っている置換は変更履歴で一つに纏められますので、戻すと纏まって戻ります。\n"
	L"・置換前に各Storyを再検索してチェックした一致が検索したときと同じ位置にあるかを確認します。変わっていたら置換を中止します。検索し直してください。\n"
	L"\n"
	L"【このプラグインの変更を一覧に戻す（Show Changes by KohakuFindChange）】\n"
	L"・メニューの Show Changes by KohakuFindChange を実行すると、その変更を一覧に戻します。前面のドキュメントか、ブックの全章かは Book Scope で決まります。\n"
	L"\n"
	L"【ブックパネル（Remember Book Panel Placement）】\n"
	L"・ON にすると、InDesign 本体のブックパネルが、前回閉じたときの位置と大きさ（アイコン化していたらアイコンのまま）で開きます。これは、ブックを閉じたときと InDesign を終了したときに記録します。";

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
	// Japanese InDesign gets the Japanese reference, everything else the English one - asked through
	// KBSLoc::JapaneseUI (see the header).
	const wchar_t* const text = KBSLoc::JapaneseUI() ? kHowToJA : kHowToEN;

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
	//
	// The cast below works because wchar_t IS a UTF-16 code unit on Windows, which is what AppendW
	// wants (the plug-in is Windows-only).
	PMString fallback;
	fallback.SetTranslatable(kFalse);
	fallback.Append("Kohaku Find/Change - How to Use\n\n");
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(text));
	CAlert::InformationAlert(fallback);
}

// End, KBSHowTo.cpp.
