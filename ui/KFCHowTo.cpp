//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  "How to Use..." on the panel's flyout menu: the plug-in's operating reference,
//  shown in a ScriptUI dialog run through the plug-in's own script engine - a multiline edit box,
//  so the text can be scrolled, selected and copied, none of which CAlert's static text can do.
//  The recipe - a private engine name, a pure-ASCII script (every non-ASCII code unit escaped as
//  \uXXXX), CAlert as the fallback - is the one KESCL's How to Use uses too.
//
//  Why the text is here and not in the string tables: odfrc caps a single string literal at about
//  3.1KB, and this reference is several times that.
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
#include "KFCHowTo.h"
#include "KFCBoundaryID.h"		// kKFCDisplayName - the page's title is built from it

namespace
{

// The page's title: the ScriptUI window's title bar, and the head of the CAlert fallback (Show). Built from the display
// name's one definition, so a rename moves it too; wide, as the reference text is (the "L" on the first piece makes the
// whole concatenation wide).
const wchar_t* const kHowToTitle = L"" kKFCDisplayName " - How to Use";

//========================================================================================
// The reference text, one wide literal per language
//========================================================================================

const wchar_t* const kHowToEN =
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.\n"
	L"\n"
	L"[Searching (Find in ...)]\n"
	L"- Open Find/Change... on the panel menu opens InDesign's own Find/Change dialog, even when no document is open.\n"
	L"- Follows InDesign's own Find/Change settings.\n"
	L"- Searches on the five tabs Text, GREP, Glyph, Object and Transliterate.\n"
	L"- The Colour tab is not supported.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON = every document of the book the Book panel is showing (the book whose tab is in front). If no Book panel can be found, the active book is used.\n"
	L"\n"
	L"[Listing QR codes (the QR Code tab)]\n"
	L"- KFC adds a QR Code tab to InDesign's own Find/Change dialog. While that tab is shown, Find in ... lists every QR code in the scope, with its kind (Web, Text, SMS, Email, vCard) and what it holds.\n"
	L"- Search: on the tab chooses All Documents or Document. With Book Scope ON, every document of the book is listed.\n"
	L"- Clicking a row (or stopping on it with the arrow keys) goes to its QR code and selects it - a closed chapter is opened.\n"
	L"- Find Next on the tab selects the next QR code, in the order of the list. After the last one it starts again from the first.\n"
	L"- QR code rows are not replaced: Replace, Change All and the other operations that change rows are greyed out.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- Right-click menu\n"
	L"- The up and down arrow keys walk the whole tree.\n"
	L"- Return replaces the selected result row; Shift+Return replaces it and then moves down to the next row that can be replaced.\n"
	L"- Ctrl+Return takes the selected result rows off the list and moves to the row below.\n"
	L"- Narrow Down on a results heading's right-click menu searches those results again with one more condition, into new results at the bottom.\n"
	L"- Search Text in These Objects on the heading of an object search's results searches the text inside those objects - each frame's own part of its story, with its tables, footnotes, anchored frames and text on a path - with the query of the tab Find/Change is on (Text, GREP, Glyph or Transliterate), into new results at the bottom. Rows taken off the list with Clear from Results are not searched.\n"
	L"- Copy Results on the panel menu copies every row of the current results, Copy Rows on a row's right-click menu that row, and Copy These Results on a results heading's right-click menu those results, to the clipboard as tab-separated text. Ctrl+C on the rows selected in the list does the same.\n"
	L"- Hide Previous Chapter: ON closes, after a jump, the windows of the searched book's other chapters that have nothing to save. A document that is not in the book, or that has been edited or never saved, is left open.\n"
	L"\n"
	L"[Selecting and replacing several rows at once]\n"
	L"- Ctrl+click adds a row to the selection. Ctrl+clicking a selected row once more takes it out.\n"
	L"- Shift+click selects every row from the row selected first to the clicked row. Shift+Down Arrow / Shift+Up Arrow widen or narrow the selection one row at a time.\n"
	L"\n"
	L"[Replacing]\n"
	L"- Follows InDesign's own Find/Change replace settings.\n"
	L"\n"
	L"[Operations on rows (Special Operations)]\n"
	L"- Special Operations on a result row's right-click menu applies InDesign's own operations to the selected rows.\n"
	L"\n"
	L"[Running saved queries in order (Run Saved Queries...)]\n"
	L"- Run Saved Queries... on the panel menu opens a dialog that lines up the queries saved in Find/Change and runs them in order.\n"
	L"- The left list (Saved Queries) shows the saved queries of the Text, GREP, Glyph, Object and Transliterate tabs. Colour queries are not supported.\n"
	L"- Add > adds a query to the end of the right list (Run Order). < Remove takes one out, Move Up / Move Down change the order, and Clear empties the right list.\n"
	L"- Find searches with the query selected in the right list. After searching, the selection moves down one row.\n"
	L"- Run (or Enter) runs the queries of the right list in order, each with InDesign's own Change All.\n"
	L"- Save Order... saves the order to a file (.json), and Load Order... loads one.\n"
	L"- A double-click on a query in the left list loads that query into InDesign's own Find/Change dialog.\n"
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
	L"・パネルメニューの Open Find/Change... で、ドキュメントが開いていなくても InDesign 本体の「検索と置換」ダイアログが開きます。\n"
	L"・InDesign 本体の検索の設定に従います。\n"
	L"・テキスト／正規表現／字形／オブジェクト／文字種変換 の5つのタブで検索できます。\n"
	L"・カラーのタブは対象外です。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝ブックパネルに表示しているブック（タブが手前にあるブック）の全ドキュメント。ブックパネルが見つからないときは、アクティブなブックを対象にします。\n"
	L"\n"
	L"【QR コードの一覧（QR Code タブ）】\n"
	L"・InDesign 本体の「検索と置換」に QR Code タブが加わります。このタブを選んでいる間、Find in … で、範囲の中の QR コードを全部、種類（Web・Text・SMS・Email・vCard）と中身つきで一覧にします。\n"
	L"・タブの Search: で All Documents／Document を選びます。Book Scope: ON なら、ブックの全ドキュメントが対象です。\n"
	L"・行をクリック（矢印キーで止まったときも）すると、その QR コードへ飛んで選びます。閉じている章も開きます。\n"
	L"・タブの Find Next で、一覧と同じ順に次の QR コードを選びます。最後の次は最初に戻ります。\n"
	L"・QR コードの行は置換しません（Replace・Change All など、書き換える操作は灰色です）。\n"
	L"\n"
	L"【結果の見方】\n"
	L"・右クリックメニュー\n"
	L"・上下の矢印キーで、ツリー全体を巡回できます。\n"
	L"・結果の行をリターンで置換、Shift＋リターンで置換した後、一つ下の置換できる行に移動します。\n"
	L"・Ctrl＋リターンで、選んだ結果の行を一覧から外し、下の行に移動します。\n"
	L"・結果の見出しの右クリックの Narrow Down で、その結果を条件を1つ足して探し直し、一番下に新しい結果として出します。\n"
	L"・オブジェクト検索の結果の見出しの右クリックの Search Text in These Objects で、そのオブジェクトの中の文字（枠に入っている部分だけ。表・脚注・アンカー付きの枠・パス上の文字も）を、「検索と置換」で開いているタブ（テキスト／正規表現／字形／文字種変換）の条件で探し、一番下に新しい結果として出します。Clear from Results で外した行は探しません。\n"
	L"・パネルメニューの Copy Results で、いまの結果の行を全部、行の右クリックの Copy Rows でその行、結果の見出しの右クリックの Copy These Results でその結果の行を、タブ区切りのテキストとしてクリップボードにコピーします。一覧で行を選んで Ctrl＋C でも同じです。\n"
	L"・Hide Previous Chapter: ON にすると、ジャンプ後に、検索したブックのジャンプ先以外の章のうち、保存するものが無いドキュメントウィンドウを閉じます。ブックに入っていないドキュメント、編集されたもの、一度も保存していないものは、そのまま残します。\n"
	L"\n"
	L"【複数の行をまとめて選ぶ・置換する】\n"
	L"・Ctrl＋クリックで行を足します。選んである行をもう一度 Ctrl＋クリックすると外れます。\n"
	L"・Shift＋クリックで、最初に選んだ行からその行までをまとめて選びます。Shift＋↓／Shift＋↑ で1行ずつ広げたり縮めたりできます。\n"
	L"\n"
	L"【置換】\n"
	L"・InDesign 本体の置換の設定に従います。\n"
	L"\n"
	L"【特殊操作（Special Operations）】\n"
	L"・結果の行の右クリックの Special Operations で、選んだ行に InDesign 本体の操作をします。\n"
	L"\n"
	L"【保存したクエリを順に実行（Run Saved Queries...）】\n"
	L"・パネルメニューの Run Saved Queries... で、「検索と置換」で保存したクエリを並べて順に実行するダイアログが開きます。\n"
	L"・左（Saved Queries）に、保存したクエリ（テキスト／正規表現／字形／オブジェクト／文字種変換）が並びます。カラーのクエリは対象外です。\n"
	L"・Add > で右（Run Order）の最後に足します。< Remove で外す、Move Up / Move Down で順番を変える、Clear で右を空にします。\n"
	L"・Find で、右で選んでいるクエリで検索します。検索した後は、選択が1つ下に移ります。\n"
	L"・Run（または Enter）で、右の順に、各クエリを InDesign 本体の「すべてを置換」で実行します。\n"
	L"・Save Order... でファイル（.json）として保存し、Load Order... で読み込めます。\n"
	L"・左のクエリをダブルクリックすると、そのクエリを InDesign 本体の「検索と置換」に読み込みます。\n"
	L"\n"
	L"【ブックパネル（Remember Book Panel Placement）】\n"
	L"・ON にすると、InDesign 本体のブックパネルが、前回閉じたときの位置と大きさ（アイコン化していたらアイコンのまま）で開きます。これは、ブックを閉じたときと InDesign を終了したときに記録します。\n"
	L"\n"
	L"【アプリケーションバーの検索欄（Link the Application Bar's Search Field to This Panel）】\n"
	L"・ON にすると、InDesign 本体のアプリケーションバーの検索欄（Adobe Stock／Adobe Help の検索欄）に、「検索と置換」でいま選んでいるタブの内容を表示します。テキスト／正規表現は検索文字列、字形は字形（文字・GID・フォント）、文字種変換は文字種です。オブジェクト／カラーのタブでは欄は変わりません。\n"
	L"・検索欄で Enter を押すと、ブラウザ（Adobe Stock／Adobe Help）を開かずに、KFC で検索します。テキスト／正規表現のタブでは、欄の文字を「検索と置換」に入れてから検索します。それ以外のタブでは検索しません。";

//========================================================================================
// Helpers
//========================================================================================

/** Append 'text' to 'out' as the body of a JavaScript double-quoted string literal: the quote and
	the backslash escaped, a line break or a tab as \n, \r or \t, and every other code unit outside
	printable ASCII as \uXXXX (a surrogate pair as two), so the generated script is pure ASCII
	whatever the text held - no encoding to get wrong on the way to the engine. */
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
	// A Japanese UI gets the Japanese reference, every other the English one - the one text of KFC's own that follows
	// the UI language (the string tables are English in every UI language - model/KFC.fr's LocaleIndex note). What is
	// asked is the UI LANGUAGE, not the featureset (PMLocaleId keeps them as separate axes - PMLocaleId.h:39-43): a
	// Roman-engine install running a Japanese UI gets the Japanese page.
	const wchar_t* const text = (LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP) ? kHowToJA : kHowToEN;

	Utils<IExtendScriptUtils> esUtils;
	if (esUtils.Exists())
	{
		// The box is editable on purpose: an editable field is guaranteed selectable, and any edit
		// dies with the dialog. A private engine name keeps this out of the user's own
		// #targetengine sessions.
		std::string js;
		js.reserve(16384);
		js += "(function () {\n"
			  "  var w = new Window(\"dialog\", \"";
		AppendJSEscaped(js, kHowToTitle);
		js += "\");\n"
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
	// reference at all. A failed run leaves no global error standing - the return code carries the
	// failure (measured with KT's scripterr probe on a throw, a parse error and a failing DOM call:
	// docs/ai-notes/kfc-block13-inspections-2026-10-20.md) - so nothing is cleared first.
	//
	// It is the one route that needs a heading. The reference itself carries none - the ScriptUI
	// window puts the title (kHowToTitle) in its title bar, so a first line saying the same
	// thing would just repeat it (the author's call) - but CAlert has no title of its own to
	// borrow: its bar says "Adobe InDesign", and the text would start mid-reference with nothing
	// naming what it belongs to. The window's own title, in both languages, so the two
	// routes are recognisably the same document.
	//
	// The casts below work because wchar_t IS a UTF-16 code unit on Windows, which is what AppendW
	// wants (the plug-in is Windows-only).
	PMString fallback;
	fallback.SetTranslatable(kFalse);
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(kHowToTitle));
	fallback.Append("\n\n");
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(text));
	CAlert::InformationAlert(fallback);
}

// End, KFCHowTo.cpp.
