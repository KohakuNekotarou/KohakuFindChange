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
// The Ruby part's own page (ShowRubyMarks).
const wchar_t* const kRubyMarksTitle = L"" kKFCDisplayName " - ルビの書き方";

const wchar_t* const kRubyMarksJA =
	L"【ルビの書き方】\n"
	L"Look For の Ruby Marks in Text（原稿に書かれた印）と Words in a Ruby List（ルビのリストのファイル）は、同じ書き方を読みます。\n"
	L"\n"
	L"　親文字《読み》\n"
	L"\n"
	L"読みは二重山括弧《 》で親文字の直後に書きます。親文字の範囲は次のように決まります。\n"
	L"\n"
	L"【｜が要らないとき】\n"
	L"《の直前に続く漢字が親文字になります（々 〆 〇 ヶ も漢字として扱います）。\n"
	L"　漢字《かんじ》　　　　「漢字」に1本（グループルビ）\n"
	L"　漢《かん》字《じ》　　「漢」と「字」に別々（モノルビ）\n"
	L"　食《た》べ物《もの》　「食」と「物」に別々（「べ」には付かない）\n"
	L"\n"
	L"【｜が要るとき】\n"
	L"｜は親文字の始まりを示します。全角の｜でも半角の | でもかまいません。\n"
	L"・親文字にかなや記号が混ざるとき\n"
	L"　｜食べ物《たべもの》　3字全体に1本\n"
	L"・漢字の連なりの途中から親文字にしたいとき\n"
	L"　小｜太郎《たろう》　　「太郎」にだけ1本（「小」には付かない）\n"
	L"\n"
	L"【モノルビかグループルビか】\n"
	L"親文字の字数で決まります。2字以上はグループルビ、1字はモノルビです。｜の有無とは関係ありません。\n"
	L"\n"
	L"【印にならないもの】\n"
	L"・読みが空のもの　漢字《》\n"
	L"・行をまたぐもの\n"
	L"・親文字が無いもの　｜《よみ》\n"
	L"・｜が無く、《の直前が漢字でないもの　かな《かな》\n"
	L"\n"
	L"【Ruby Marks in Text（原稿の印をルビにする）】\n"
	L"Find in … で印を一覧にします。この時点では文書は変わりません。行の Replace で、親文字にルビを付けて印を文字から消します。一緒に置換した行は Ctrl+Z 1回で戻ります。Track Changes が入っている物語では、消した印が残るので置換しません。\n"
	L"\n"
	L"【Words in a Ruby List（リストのファイルから付ける）】\n"
	L"すでにある文字に、ファイルに書いた読みをまとめて付けます。Choose List... でファイルを選びます。\n"
	L"\n"
	L"　太《た》郎《ろう》\n"
	L"　太《ふと》る\n"
	L"　琥《こ》珀《はく》\n"
	L"　猫《ねこ》\n"
	L"\n"
	L"・1行に1語です。印を除いた字（太郎、太る）を本文から探し、印の付いた字にだけルビを付けます。太《ふと》る は「太る」を探して「太」にだけ付けます。「る」は探すための字です。\n"
	L"・ファイルでは《 》の代わりに ＜＜ ＞＞、<< >>、≪ ≫ でも書けます。\n"
	L"・長い語から先に当てます。太《ふと》 と 太《た》郎《ろう》 の両方があれば、「太郎」は「たろう」です。ファイルの順番は関係ありません。\n"
	L"・同じ語に2つの読みは書けません。前後の字を含めて書き分けます（行《い》く、行《こ》う）。\n"
	L"・空の行と、# で始まる行は読み飛ばします。CSV やタブ区切りは1列目だけを読みます。文字コードは UTF-8、UTF-16、Shift_JIS です。\n"
	L"・読めない行があるとファイル全体を使わず、最初の行の番号を言います。\n"
	L"・Find in … は、語の出現を全部一覧にし、付く読みを行の上に薄く出します。すでにルビのある箇所は一覧に出さず、数だけ言います。\n"
	L"・行の Replace で付けます。一緒に置換した行は Ctrl+Z 1回で戻ります。行は一覧を作ったときの読みで書きます。ファイルを直したら Find in … をやり直してください。\n"
	L"・語の一部に当たることがあります（「太郎」は「小太郎」にも当たります）。「小太郎」の行を足すか、その行を選ばずに Replace してください。\n"
	L"・Occurrences で、語ごとに最初の1か所だけを一覧にできます（First in Story、First in Document、First in Book）。最初とは一覧に並ぶ順の最初です（ページ順）。最初の1か所にすでにルビがあれば、その語は一覧に出しません。Book Scope が切のときの First in Book は First in Document と同じです。\n"
	L"\n"
	L"【Save List...（文書のルビをリストに書き出す）】\n"
	L"Search の範囲（Book Scope が入っていればブック）のルビを、このリストの書き方でファイルに書き出します。文書は変わりません。\n"
	L"・同じ語は1行です。接したルビは1語にまとめます。\n"
	L"・同じ語に別の読みがあれば、語の次の1字を付けて書き分けます（太《ふと》る、太《た》田）。読みが1つの語には付けません。\n"
	L"・次の字が空白や段落の終わりの箇所は、語だけの行になります。それでも同じ語に別の読みが残れば、多い方を行にし、ほかは # の行に箇所の数と一緒に残します（# の行は読むときに飛ばされます）。読みの揺れの点検にも使えます。\n"
	L"・タブのアイコン（ロック・非表示・親ページ・脚注）の設定に従います。\n";

// The Kenten part's own page (ShowKenten).
const wchar_t* const kKentenTitle = L"" kKFCDisplayName " - 圏点の探し方と付け方";

const wchar_t* const kKentenJA =
	L"【圏点の探し方と付け方】\n"
	L"Find: Kenten は、圏点の付いた文字を探し、圏点の種類を変えたり外したりします。\n"
	L"Find in … で一覧にします。この時点では文書は変わりません。行の Replace で書きます。\n"
	L"\n"
	L"【Look For】\n"
	L"・Text With Kenten　圏点の付いた文字を探します。同じ種類の圏点が続く範囲が1行になります。文字スタイルや段落スタイルの圏点も探します。段落の終わりで行が分かれます。\n"
	L"・Text Without Kenten　Base Text の文字のうち、圏点が付いていない箇所を探します。Base Text が空のときは探せません。\n"
	L"\n"
	L"【Search の下のアイコン】\n"
	L"本体のテキストタブと同じ働きです。ロックされたレイヤーとオブジェクト（検索のみ）、ロックされたストーリー（検索のみ）、非表示のレイヤーとオブジェクト、親ページ、脚注を探す範囲に入れるかと、カナの区別、全角・半角の区別です。Ruby とは別に覚えます。\n"
	L"\n"
	L"【Base Text】\n"
	L"Text With Kenten では、入れた文字を含む行だけを出します。空なら全部です。\n"
	L"\n"
	L"【Kenten】\n"
	L"探す圏点の種類です。[Any Kenten] はどの種類でも探します。右に選んだ印の見本が出ます。Text Without Kenten では使いません。\n"
	L"\n"
	L"【Change Kenten To】\n"
	L"行の Replace で付ける圏点の種類です。右に選んだ印の見本が出ます。[Leave As It Is] なら種類を変えません。\n"
	L"種類だけを書きます。圏点の大きさ・位置・色などの設定は変えません。\n"
	L"\n"
	L"【Remove Kenten】\n"
	L"入れると、行の Replace で圏点を外します。Change Kenten To より優先します。\n"
	L"圏点に関する設定（大きさ・フォント・色なども）を全部外し、本文に残しません。文字スタイルや段落スタイルの圏点は、スタイルを変えず、その文字の上で「なし」にします。\n"
	L"\n"
	L"【Replace】\n"
	L"行の右クリックメニューの Replace か、Return で書きます。書くのはその時のタブの設定です。\n"
	L"一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"Change Kenten To が [Leave As It Is] で Remove Kenten も入っていないときや、すでにその種類のときは、書かずにメッセージで言います。\n"
	L"\n"
	L"【一覧の見方】\n"
	L"行は2段です。下の段が親文字、上の段が文字ごとの印です。圏点を外した行は、上の段に棒が出ます。\n";

//========================================================================================
// The reference text, one wide literal per language
//========================================================================================

const wchar_t* const kHowToEN =
	L"DISCLAIMER: We cannot take responsibility for any problems that may arise. Use at your own risk.\n"
	L"\n"
	L"[Searching (Find in ...)]\n"
	L"- Open Find/Change... on the panel menu opens InDesign's own Find/Change dialog, even when no document is open.\n"
	L"- Follows InDesign's own Find/Change settings.\n"
	L"- Searches on the tabs Text, GREP, Glyph, Object, Transliterate and Kohaku+ (KFC's own).\n"
	L"- The Colour tab is not supported.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON = every document of the book the Book panel is showing (the book whose tab is in front). If no Book panel can be found, the active book is used.\n"
	L"\n"
	L"[The Kohaku+ tab]\n"
	L"- KFC adds a Kohaku+ tab to InDesign's own Find/Change dialog. While it is shown, Find in ... searches what its Find: says: QR Code, Table and, in a Japanese InDesign, Ruby and Kenten.\n"
	L"- Search: on the tab says where. QR codes: All Documents or Document. Tables, ruby and kenten: those, Story, To End of Story or Selection - one the selection does not offer searches the document, and says so. With Book Scope ON, every document of the book is listed.\n"
	L"\n"
	L"- Saving what the tab asks: Save Query on Find/Change's Query line. It is then on Find/Change's Query menu (as \"Table - <name>\", \"Kenten - <name>\" and so on) - pick it, and this tab opens and is set - and in Run Saved Queries...: its Find lists, its Run writes the Change side with no list, among the other queries.\n"
	L"\n"
	L"[Listing QR codes (Find: QR Code)]\n"
	L"- Find in ... lists the QR codes in the scope, with their kind (Web, Text, SMS, Email, vCard) and what they hold.\n"
	L"- Clicking a row goes to its QR code and selects it.\n"
	L"\n"
	L"[Finding tables by their table style (Find: Table)]\n"
	L"- Find in ... lists the tables in the scope that have its Find Table Style (and With Overrides Only), with their size and style - a + marks a table with overrides.\n"
	L"- Replace on a row (its right-click menu, or Return) gives its table the tab's Change to Table Style and Clear Overrides, as they stand then. One Ctrl+Z takes back the rows replaced together.\n"
	L"- Search Text in These Tables on the heading of those results searches the text in those tables with the query of the tab Find/Change is on (Text, GREP, Glyph or Transliterate), into new results at the bottom.\n"
	L"- The tab's own buttons work as on Find/Change's Text tab, over its Search: Find Next goes to the next table and selects it, Change gives the table it found the Change side, Change/Find does that and goes on, and Change All gives every table of the Search: the Change side. One Ctrl+Z takes each back.\n"
	L"\n"
	L"[Finding and setting ruby (Find: Ruby - a Japanese InDesign)]\n"
	L"- Find in ... lists the text with ruby (Base Text and Ruby: what its text and its reading contain, each row of boxes read as one), or, with Text Without Ruby, each occurrence of Base Text that has none.\n"
	L"- Replace on a row gives it what the tab's Change Ruby To boxes hold, as they stand then - or, with no reading in them, takes its ruby off. One Ctrl+Z takes back the rows replaced together.\n"
	L"- Ruby and Base Text are rows of boxes, a Base Text box holding one character - more, typed or pasted, go on into the next boxes; + and - add and take away boxes. Change Ruby To's boxes follow them, with the Base Text shown under each. A reading goes on the base text under it. The chain between two boxes joins them, on both sides: one reading over all their base text (group ruby). A row whose text is not what the base boxes hold is left, and said; clear those boxes to give the reading to the whole row.\n"
	L"- Ruby Marks in Text lists ruby a manuscript wrote out, as \uFF5C\u6F22\u5B57\u300A\u304B\u3093\u3058\u300B or \u6F22\u5B57\u300A\u304B\u3093\u3058\u300B (with no bar, the kanji just before it). Replace on its row puts the reading on the base and takes the marks out.\n"
	L"- Words in a Ruby List reads a file chosen with Choose List...: a word a line, written as those marks. It lists each occurrence of a word that has no ruby, the reading it will get faint over it (the longest word at a place wins), and Replace on a row writes it.\n"
	L"- Words in a Ruby List's Occurrences: can take each word's first alone: First in Story, First in Document, First in Book. A word whose first has ruby has no row.\n"
	L"- Save List... writes the ruby of what Search: holds (the book, with Book Scope on) as such a file: each word once. A word read two ways has a line for each, with the character after it; what that cannot tell apart is a line the way most of it reads, and # lines under it.\n"
	L"- The icons under Search: do what they do on InDesign's Text tab: locked layers and objects, locked stories (both Search Only), hidden layers and objects, parent pages, footnotes, and Kana and Width Sensitive. Ruby and Kenten each hold their own.\n"
	L"- How to Use..., left of Done on the tab, says how the marks are written (in Japanese).\n"
	L"\n"
	L"[Finding and setting kenten (Find: Kenten - a Japanese InDesign)]\n"
	L"- Find in ... lists the text with emphasis marks (Base Text: what its text contains; Kenten: the kind), or, with Text Without Kenten, each occurrence of Base Text that has none.\n"
	L"- Replace on a row gives it the tab's Change Kenten To, or takes the marks off with Remove Kenten, as they stand then. One Ctrl+Z takes back the rows replaced together.\n"
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
	L"- After a Run the panel lists each place it wrote: what the place has been (cat → dog → kog) and the queries that wrote it. A row goes to its place; nothing is written from these rows.\n"
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
	L"・テキスト／正規表現／字形／オブジェクト／文字種変換／Kohaku+（KFC のタブ）で検索できます。\n"
	L"・カラーのタブは対象外です。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝ブックパネルに表示しているブック（タブが手前にあるブック）の全ドキュメント。ブックパネルが見つからないときは、アクティブなブックを対象にします。\n"
	L"\n"
	L"【Kohaku+ タブ】\n"
	L"・InDesign 本体の「検索と置換」に Kohaku+ タブが加わります。このタブを選んでいる間、Find in … は、タブの Find: で選んだものを探します（QR Code／Table／Ruby／Kenten）。\n"
	L"・タブの Search: で範囲を選びます。QR コードは All Documents／Document。表とルビと圏点はそれに加えて Story／To End of Story／Selection。今の選択で選べない範囲はドキュメント全体を探し、そう書きます。Book Scope: ON なら、ブックの全ドキュメントが対象です。\n"
	L"\n"
	L"・タブの条件を保存するには、検索と置換のクエリの行の保存ボタンを押します。本体のクエリのメニューに「Table - 名前」「Kenten - 名前」のように並び、選ぶとこのタブが開いて条件が入ります。Run Saved Queries... の一覧にも出て、Find は一覧を作り、Run は一覧を作らずに Change 側を書きます（他のクエリと一緒に回せます）。\n"
	L"\n"
	L"【QR コードの一覧（Find: QR Code）】\n"
	L"・Find in … で、範囲の中の QR コードを、種類（Web・Text・SMS・Email・vCard）と中身つきで一覧にします。\n"
	L"・行をクリックすると、その QR コードへ飛んで選びます。\n"
	L"\n"
	L"【表スタイルで表を探す（Find: Table）】\n"
	L"・Find in … で、範囲の中の表のうち Find Table Style（と With Overrides Only）に合うものを、大きさと表スタイルつきで一覧にします。+ は上書きのある表です。\n"
	L"・行の Replace（右クリックメニュー、または Return）で、その表にタブの Change to Table Style と Clear Overrides を、その時の設定で当てます。一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"・見出しの右クリックの Search Text in These Tables で、その表の中の文字を、本体の検索と置換のタブ（テキスト／正規表現／字形／文字種変換）の条件で探し、新しい結果を下に作ります。\n"
	L"・タブのボタンは本体のテキストタブと同じ働きで、Search の範囲にかかります。Find Next で次の表へ行って選び、Change で見つけた表に Change 側を当て、Change/Find は当てて次へ進み、Change All は範囲の表すべてに当てます。どれも Ctrl+Z 1回で戻ります。\n"
	L"\n"
	L"【ルビで探す・ルビを付ける（Find: Ruby）】\n"
	L"・Find in … で、ルビの付いた文字を一覧にします（Base Text は親文字、Ruby は読みに含まれる文字）。Text Without Ruby にすると、Base Text のうちルビの付いていない箇所を一覧にします。\n"
	L"・行の Replace で、その箇所にタブの Change Ruby To の欄のルビを付けます。読みの欄が全部空ならルビを外します（どちらもその時の設定）。一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"・Ruby と Base Text は欄の並びで（Base Text の欄は1字ずつ。2字以上を打つか貼ると次の欄へ続きます）、＋と−で欄を増減できます（探すときは欄をつないだ字で比べます）。Change Ruby To の欄はこれに連動し、その下に Base Text の字が出ます。読みは、その下の親文字に付きます。欄の間の鎖を押すと両方の欄がつながり、1つの読みを下の親文字全部に付けます（グループルビ）。行の文字が親文字の欄と違う行は書かずにメッセージで言います（親文字の欄を空にすると、行の文字全部に読みを付けます）。\n"
	L"・Ruby Marks in Text は、原稿に書かれたルビの印（｜漢字《かんじ》、｜が無ければ《の直前の漢字）を一覧にします。行の Replace で、親文字にそのルビを付けて印を消します。\n"
	L"・Words in a Ruby List は、Choose List... で選んだファイル（1行1語・同じ印の書き方）の語を探します。ルビのない出現を全部一覧にし、付く読みを行の上に薄く出します（長い語が優先）。行の Replace で付けます。\n"
	L"・Words in a Ruby List の Occurrences で、語ごとに最初の1か所だけを一覧にできます（First in Story、First in Document、First in Book）。\n"
	L"・Save List... で、Search の範囲（Book Scope が入っていればブック）のルビをこのリストの形でファイルに書き出します。同じ語は1行、別の読みがあれば次の1字を付けて書き分け、それでも分からなければ # の行に残します。\n"
	L"・Search の下のアイコンは本体のテキストタブと同じ働きです。Ruby と Kenten で別々に覚えます。\n"
	L"・タブの完了ボタンの左の How to Use... に印の書き方があります。\n"
	L"\n"
	L"【圏点で探す・圏点を付ける（Find: Kenten）】\n"
	L"・Find in … で、圏点の付いた文字を一覧にします（Base Text は含まれる文字、Kenten は圏点の種類）。Text Without Kenten にすると、Base Text のうち圏点の付いていない箇所を一覧にします。\n"
	L"・行の Replace で、その箇所にタブの Change Kenten To の圏点を付けます。Remove Kenten なら圏点を外します（どちらもその時の設定）。一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"・タブの完了ボタンの左の How to Use... に欄の説明があります。\n"
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
	L"・Run の後、パネルに書いた場所の一覧が出ます。その場所が何から何になったか（cat → dog → kog）と、書いたクエリの名前が読めます。行から本文へ飛べます。行から書き込みはできません。\n"
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

namespace { void ShowPage(const wchar_t* title, const wchar_t* text); }

//========================================================================================
// KFCHowTo::Show
//========================================================================================

void KFCHowTo::Show()
{
	// A Japanese UI gets the Japanese reference, every other the English one - the one text of KFC's own that follows
	// the UI language (the string tables are English in every UI language - model/KFC.fr's LocaleIndex note). What is
	// asked is the UI LANGUAGE, not the featureset (PMLocaleId keeps them as separate axes - PMLocaleId.h:39-43): a
	// Roman-engine install running a Japanese UI gets the Japanese page.
	ShowPage(kHowToTitle, (LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP) ? kHowToJA : kHowToEN);
}

void KFCHowTo::ShowRubyMarks()
{
	ShowPage(kRubyMarksTitle, kRubyMarksJA);
}

void KFCHowTo::ShowKenten()
{
	ShowPage(kKentenTitle, kKentenJA);
}

namespace
{
void ShowPage(const wchar_t* title, const wchar_t* text)
{
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
		AppendJSEscaped(js, title);
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
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(title));
	fallback.Append("\n\n");
	fallback.AppendW(reinterpret_cast<const UTF16TextChar*>(text));
	CAlert::InformationAlert(fallback);
}
}	// anonymous namespace

// End, KFCHowTo.cpp.
