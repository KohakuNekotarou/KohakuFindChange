//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  "How to Use..." on the panel's flyout menu: the plug-in's operating reference,
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
#include "KFCHowTo.h"
#include "KFCLoc.h"					// JapaneseUI - the one place "is the UI Japanese" is asked

namespace
{

//========================================================================================
// The reference text, one wide literal per language
//========================================================================================

const wchar_t* const kHowToEN =
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.\n"
	L"\n"
	L"[Searching (Find in ...)]\n"
	L"- Open Find/Change... on the panel menu opens InDesign's own Find/Change dialog, even when no document is open.\n"
	L"- Follows InDesign's own Find/Change settings.\n"
	L"- Searches on the four tabs Text, GREP, Glyph and Transliterate.\n"
	L"- The Object and Colour tabs are not supported.\n"
	L"- A search stops at 300 matches. For more, narrow the search, or use a Change All.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON = every document of the book the Book panel is showing (the book whose tab is in front). If no Book panel can be found, the active book is used.\n"
	L"- Book Scope: OFF = what Find/Change's Search: names (Document, All Documents, Story or Selection). The menu item's name says which.\n"
	L"\n"
	L"[Selected documents in a book (Find/Change Selected Documents (Book))]\n"
	L"- With Book Scope and this ON, Find, Change All in Book and Run Saved Queries... take only the documents selected in the Book panel (select its rows with a click, Shift+click or Ctrl+click). With none or all of them selected, the whole book is used.\n"
	L"- While it applies, the menu says Find in Selected Documents and Change All in Selected Documents (No List). Save Panel Settings keeps this setting.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- A double-click selects the matched text.\n"
	L"- After a GREP search, selecting a match shows above the list what its Replace would write there (Preview Text) - nothing is written until you replace it.\n"
	L"- The up and down arrow keys walk the whole tree, opening the rows they pass through.\n"
	L"- Hide Previous Chapter: ON closes, after a jump, the document windows other than the one jumped to that have nothing to save. A document that has been edited, or has never been saved, is left open.\n"
	L"\n"
	L"[Replacing]\n"
	L"- Follows InDesign's own Find/Change replace settings.\n"
	L"- Replace one match at a time: right-click its row and choose Replace, or select the row and press Return - a replaced row then starts with Changed. The arrow keys walk the rows, and each row shows its match in the document as you land on it. Ctrl+Z undoes a replace, and the panel follows Ctrl+Z and Redo.\n"
	L"- Change All in Book (No List) writes every match in the book with InDesign's own Change All (Book Scope on), while the panel shows no list - choose Clear Results first. The message says how many were replaced in each chapter. For a document, use Change All in InDesign's Find/Change dialog.\n"
	L"- Track Changes is left as each story has it: a replacement is recorded only in a story where Track Changes is on, under your user name, as InDesign's own Change does.\n"
	L"- Before a row is replaced, its story is searched again to check that the match is still where it was found. If it has moved, nothing is replaced. Search again.\n"
	L"\n"
	L"[Saved queries (Run Saved Queries...)]\n"
	L"- Run Saved Queries... on the panel menu opens a dialog that can stay open while you work (it has a minimize button). Left: the queries saved in Find/Change - Text, GREP, Glyph and Transliterate, yours and InDesign's own. Right: the order to run them in, set with Add >, < Remove, Move Up, Move Down and Clear. The order is kept after the dialog is closed and after InDesign quits.\n"
	L"- The line at the bottom (Runs on:) says what Run will write: the book with Book Scope on, otherwise what Find/Change's Search: names. Run (or Enter) runs the queries in that order, each with InDesign's own Change All, and the panel and the dialog's own message line say how many each query replaced. One Ctrl+Z undoes the whole run.\n"
	L"\n"
	L"[The Book panel (Remember Book Panel Placement)]\n"
	L"- ON: InDesign's own Book panel opens where it was and at the size it was (collapsed to icons, if it was) when it was last closed. This is recorded when a book is closed and when InDesign quits.\n"
	L"\n"
	L"[The application bar's search field (Link the Application Bar's Search Field to This Panel)]\n"
	L"- ON: the search field on InDesign's application bar (the Adobe Stock / Adobe Help one) shows what the tab Find/Change is on holds: the find string on Text and GREP, the glyph (character, GID, font) on Glyph, the character type on Transliterate. On the Object and Colour tabs the field is left as it is.\n"
	L"- Return in the field searches with KFC instead of opening a browser (Adobe Stock / Adobe Help). On the Text and GREP tabs the field's text is put into Find/Change first; on the other tabs nothing is searched.";

const wchar_t* const kHowToJA =
	L"【免責】 どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。\n"
	L"\n"
	L"【検索（Find in …）】\n"
	L"・パネルメニューの Open Find/Change... で、ドキュメントが開いていなくてもInDesign 本体の「検索と置換」ダイアログが開きます。\n"
	L"・InDesign本体の検索の設定に従います。\n"
	L"・テキスト / 正規表現 / 字形 / 文字種変換 の4つのタブで検索できます。\n"
	L"・オブジェクト／カラーのタブは対象外です。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝ブックパネルに表示しているブック（タブが手前にあるブック）の全ドキュメント。ブックパネルが見つからないときは、アクティブなブックを対象にします。\n"
	L"・Book Scope: OFF＝「検索と置換」の「検索:」の対象（ドキュメント／すべてのドキュメント／ストーリー／選択範囲）。メニューの名前に出ます。\n"
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
	L"・ON にすると、InDesign 本体のブックパネルが、前回閉じたときの位置と大きさ（アイコン化していたらアイコンのまま）で開きます。これは、ブックを閉じたときと InDesign を終了したときに記録します。\n"
	L"\n"
	L"【アプリケーションバーの検索欄（Link the Application Bar's Search Field to This Panel）】\n"
	L"・ON にすると、InDesign 本体のアプリケーションバーの検索欄（Adobe Stock／Adobe Help の検索欄）に、「検索と置換」でいま選んでいるタブの内容を表示します。テキスト／正規表現は検索文字列、字形は字形（文字・GID・フォント）、文字種変換は文字種です。オブジェクト／カラーのタブでは欄は変わりません。\n"
	L"・検索欄で Enter を押すと、ブラウザ（Adobe Stock／Adobe Help）を開かずに、KFCで検索します。テキスト／正規表現のタブでは、欄の文字を「検索と置換」に入れてから検索します。それ以外のタブでは検索しません。";

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
// KFCHowTo::Show
//========================================================================================

void KFCHowTo::Show()
{
	// Japanese InDesign gets the Japanese reference, everything else the English one - asked through
	// KFCLoc::JapaneseUI (see the header).
	const wchar_t* const text = KFCLoc::JapaneseUI() ? kHowToJA : kHowToEN;

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
		if (esUtils->RunScriptInEngine("KFC", scriptText, kFalse /*showErrorAlert*/, kFalse) == kSuccess)
			return;
	}

	// Script route unavailable or failed: the plain (non-scrollable) alert is better than no
	// reference at all.
	//
	// It is the one route that needs a heading. The reference itself carries none - the ScriptUI
	// window puts "Kohaku Find/Change - How to Use" in its title bar, so a first line saying the same
	// thing would just repeat it (the author's call) - but CAlert has no title of its own to
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

// End, KFCHowTo.cpp.
