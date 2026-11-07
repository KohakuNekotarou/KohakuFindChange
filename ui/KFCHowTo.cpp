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
#include <cwchar>				// wcslen - Run Script's Help's length

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
	L"「検索」の一覧の「ルビ印」（原稿に書かれた印）と「ルビ一覧」（ルビのリストのファイル）は、同じ書き方を読みます。\n"
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
	L"【「ルビ印」（原稿の印をルビにする）】\n"
	L"Find in … で印を一覧にします。この時点では文書は変わりません。行の Replace で、親文字にルビを付けて印を文字から消します。一緒に置換した行は Ctrl+Z 1回で戻ります。Track Changes が入っている物語では、消した印が残るので置換しません。\n"
	L"\n"
	L"【「ルビ一覧」（リストのファイルの読みを付ける）】\n"
	L"すでにある文字に、ファイルに書いた読みをまとめて付けます。Choose List... でテキストファイル（.txt か .csv）を選びます。\n"
	L"\n"
	L"　太《た》郎《ろう》\n"
	L"　太《ふと》る\n"
	L"　琥《こ》珀《はく》\n"
	L"　猫《ねこ》\n"
	L"　東京《とうきょう》\n"
	L"　｜五月雨《さみだれ》\n"
	L"\n"
	L"・1行に1語です。印を除いた字（太郎、太る）を本文から探し、印の付いた字にだけルビを付けます。太《ふと》る は「太る」を探して「太」にだけ付けます。「る」は探すための字です。\n"
	L"・《》の前の親文字が2字以上なら、1つの読みを全体に付けるグループルビです（東京《とうきょう》、｜五月雨《さみだれ》）。1字ずつ《》を書けば、1字ずつのモノルビです（太《た》郎《ろう》、琥《こ》珀《はく》）。親文字の範囲の決まりは上の【｜が要らないとき】【｜が要るとき】と同じです。\n"
	L"・ファイルでは《 》の代わりに ＜＜ ＞＞、<< >>、≪ ≫ でも書けます。\n"
	L"・長い語から先に当てます。太《ふと》 と 太《た》郎《ろう》 の両方があれば、「太郎」は「たろう」です。ファイルの順番は関係ありません。\n"
	L"・同じ語に2つの読みは書けません。前後の字を含めて書き分けます（行《い》く、行《こ》う）。\n"
	L"・空の行と、# で始まる行は読み飛ばします。CSV やタブ区切りは1列目だけを読みます。文字コードは UTF-8、UTF-16、Shift_JIS です。\n"
	L"・読めない行があるとファイル全体を使わず、最初の行の番号を言います。\n"
	L"・Find in … は、語の出現を全部一覧にし、付く読みを行の上に薄く出します。すでにルビのある箇所は一覧に出さず、数だけ言います。\n"
	L"・行の Replace で付けます。一緒に置換した行は Ctrl+Z 1回で戻ります。行は一覧を作ったときの読みで書きます。ファイルを直したら Find in … をやり直してください。\n"
	L"・語の一部に当たることがあります（「太郎」は「小太郎」にも当たります）。「小太郎」の行を足すか、その行を選ばずに Replace してください。\n"
	L"・「対象」で、語ごとに最初の1か所だけを一覧にできます（ストーリーで最初、ドキュメントで最初、ブックで最初）。最初とは一覧に並ぶ順の最初です（ページ順）。最初の1か所にすでにルビがあれば、その語は一覧に出しません。Book Scope が切のときの「ブックで最初」は「ドキュメントで最初」と同じです。\n"
	L"・「違う読みのルビも含める」を入れると、リストと違う読みのルビが付いた箇所も一覧にします（行は「今のルビ → リストの読み」）。行の Replace でリストの読みに直します。リストと同じ読みのルビ（グループルビかモノルビかは見ません）や、語の外まで続くルビは、数だけ言います。\n"
	L"\n"
	L"【Save List...（文書のルビをリストに書き出す）】\n"
	L"Search の範囲（Book Scope が入っていればブック）のルビを、このリストの書き方でファイルに書き出します。文書は変わりません。\n"
	L"・同じ語は1行です。接したルビは1語にまとめます。\n"
	L"・同じ語に別の読みがあれば、語の次の1字を付けて書き分けます（太《ふと》る、太《た》田）。読みが1つの語には付けません。\n"
	L"・次の字が空白や段落の終わりの箇所は、語だけの行になります。それでも同じ語に別の読みが残れば、多い方を行にし、ほかは # の行に箇所の数と一緒に残します（# の行は読むときに飛ばされます）。読みの揺れの点検にも使えます。\n"
	L"・タブのアイコン（ロック・非表示・親ページ・脚注）の設定に従います。\n"
	L"\n"
	L"【「ルビ揺れ」で読みの揺れを探す】\n"
	L"Search の範囲（Book Scope が入っていればブック）で、同じ親文字に2通り以上の読みがある語を探します。検索の欄は空のままで探せます。親文字の欄に字を入れると、その字を含む語だけに絞れます（ルビの欄は使わないので灰色です）。読みはグループルビかモノルビかを見ずに比べ、カナや全角・半角の区別はタブのアイコンに従います。\n"
	L"一覧は読みごとの見出し（太郎《たろう》、太郎《たろ》など）にまとまり、見出しの下にその読みで見つかった箇所が並びます。同じ語の見出しは並び、数の多い読みが先です。\n"
	L"そろえるには、見出しか行を右クリックして、メニューに出る「《たろう》」のような、その語のほかの読みを選びます。見出しなら下の箇所が全部、行なら選んだ行（同じ語のもの）がその読みになります。Ctrl+Z 1回で戻ります。すでにその読みの箇所は書かずに数えるだけです。\n"
	L"「ルビ揺れ」には「置換」のルビの欄がありません。行の Replace と Return、タブの Change／Change All／Change/Find は使えません（灰色）。保存したクエリも、Run Saved Queries の Run では飛ばします。\n"
	L"\n"
	L"【検索のみ】\n"
	L"「検索」の一覧が「ルビ有」「ルビ無」のときだけ出ます（ルビ印・ルビ一覧・ルビ揺れでは隠れ、効きません。チェックは覚えていて、戻すとまた出ます）。入れると探すだけになります。「置換」のルビの欄と、Change／Change All／Change/Find が灰色になります。行の Replace は書かずに、その理由を言います。保存したクエリにも入り、Run Saved Queries の Run では飛ばして、そう言います。\n";

// The Kenten part's own page (ShowKenten).
const wchar_t* const kKentenTitle = L"" kKFCDisplayName " - 圏点の探し方と付け方";

const wchar_t* const kKentenJA =
	L"【圏点の探し方と付け方】\n"
	L"Find: Kenten は、圏点の付いた文字を探し、圏点の種類を変えたり外したりします。\n"
	L"Find in … で一覧にします。この時点では文書は変わりません。行の Replace で書きます。\n"
	L"\n"
	L"【Search の下のアイコン】\n"
	L"本体のテキストタブと同じ働きです。ロックされたレイヤーとオブジェクト（検索のみ）、ロックされたストーリー（検索のみ）、非表示のレイヤーとオブジェクト、親ページ、脚注を探す範囲に入れるかと、カナの区別、全角・半角の区別です。Ruby とは別に覚えます。\n"
	L"\n"
	L"【親文字】\n"
	L"圏点の付いた文字を探すときは、入れた文字を含む行だけを出します。空なら全部です。\n"
	L"\n"
	L"【圏点】\n"
	L"探す圏点の種類です。種類の名前と並びは InDesign の圏点設定と同じです。\n"
	L"・[すべての圏点]　どの種類でも探します。同じ種類の圏点が続く範囲が1行になります。文字スタイルや段落スタイルの圏点も探します。段落の終わりで行が分かれます。\n"
	L"・なし　親文字の欄の文字のうち、圏点が付いていない箇所を探します。親文字の欄が空のときは探せません。\n"
	L"・カスタム　右の□に字を入れると、その字のカスタム圏点だけを探します。□が空ならどのカスタム圏点も探します。字の違うカスタム圏点が続く所は別の行になります。\n"
	L"\n"
	L"【置換圏点】\n"
	L"行の Replace で付ける圏点の種類です。[変更しない] なら種類を変えません。\n"
	L"種類だけを書きます。圏点の大きさ・位置・色などの設定は変えません。\n"
	L"・なし　圏点を外します。圏点に関する設定（大きさ・フォント・色なども）を全部外し、本文に残しません。文字スタイルや段落スタイルの圏点は、スタイルを変えず、その文字の上で「なし」にします。\n"
	L"・カスタム　右の□の字のカスタム圏点を付けます（文字の入力方法は「直接入力」）。□が空なら付けません。\n"
	L"\n"
	L"【□（カスタムの字）】\n"
	L"カスタムを選んだときだけ使えます。1文字だけで、新しく打った字・貼った字に置き換わります。InDesign のカスタム圏点は U+FFFF までの1字なので、それを超える字（𠮟・絵文字など）は使えません。異体字セレクタは付きません。\n"
	L"\n"
	L"【Replace】\n"
	L"行の右クリックメニューの Replace か、Return で書きます。書くのはその時のタブの設定です。\n"
	L"一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"置換圏点が [変更しない] のときや、カスタムで□が空のとき、すでにそのとおりのときは、書かずにメッセージで言います。\n"
	L"\n"
	L"【タブのボタン】\n"
	L"一番上の Find in KFC は、このパネルの Find in … をタブの条件で走らせます。Find Next で次の箇所へ行って選び、Change で見つけた箇所に置換圏点を当て、Change/Find は当てて次へ進み、Change All は範囲の全部に当てます。どれも Ctrl+Z 1回で戻ります。\n"
	L"\n"
	L"【一覧の見方】\n"
	L"行は2段です。下の段が親文字、上の段が文字ごとの印です。印の形と大きさの割合は InDesign の圏点と同じです（見分けやすいよう、親文字に対しては InDesign より大きめ）。カスタム圏点はその字で出ます。圏点を外した行は、上の段に棒が出ます。\n";

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
	L"[Replacing]\n"
	L"- Follows InDesign's own Find/Change replace settings.\n"
	L"- Change All in Book on the panel menu (Book Scope on) replaces in every chapter of the book with InDesign's own Change All, and lists the places it replaced, each row reading what it was and what it is now - a list already in the panel is cleared by it first. One Ctrl+Z takes it all back, the list it cleared too.\n"
	L"\n"
	L"[Scope (Book Scope)]\n"
	L"- Book Scope: ON = every document of the book the Book panel is showing (the book whose tab is in front). If no Book panel can be found, the active book is used.\n"
	L"\n"
	L"[Reading the results]\n"
	L"- Right-click menu\n"
	L"- The up and down arrow keys walk the whole tree.\n"
	L"- Return replaces the selected result row; Shift+Return replaces it and then moves down to the next row that can be replaced.\n"
	L"- Ctrl+Return takes the selected result rows off the list and moves to the row below.\n"
	L"- Narrow Down on a results heading's right-click menu searches those results again with one more condition, into new results at the bottom.\n"
	L"- Search Text in These Objects on the heading of an object search's results searches the text inside those objects - each frame's own part of its story, with its tables, footnotes, anchored frames and text on a path - with the query of the tab Find/Change is on (Text, GREP, Glyph or Transliterate), into new results at the bottom. Rows taken off the list with Clear from Results are not searched.\n"
	L"- Hide Previous Chapter: ON closes, after a jump, the windows of the searched book's other chapters that have nothing to save. A document that is not in the book, or that has been edited or never saved, is left open.\n"
	L"- Open Without a Window (the book row's right-click menu, a toggle - OFF at first): ON, a click on a row of the book's results gives its chapter no window - a closed chapter is opened without one, nothing is selected, and the message area says so. The chapter is then yours: Special Operations > Edit in Story Editor opens just its story, and Find: Document finds it (Show, Close).\n"
	L"\n"
	L"[Selecting and replacing several rows at once]\n"
	L"- Ctrl+click adds a row to the selection. Ctrl+clicking a selected row once more takes it out.\n"
	L"- Shift+click selects every row from the row selected first to the clicked row. Shift+Down Arrow / Shift+Up Arrow widen or narrow the selection one row at a time.\n"
	L"- Replace All Rows on a story or document row's right-click menu replaces every row under it at once, under a closed row too. A row that cannot be replaced is left as it is, and said. One Ctrl+Z takes them all back.\n"
	L"\n"
	L"[Operations on rows (Special Operations)]\n"
	L"- Special Operations on a result row's right-click menu applies InDesign's own operations to the selected rows.\n"
	L"\n"
	L"[Running a script on rows (Special Operations > Run Script...)]\n"
	L"- Special Operations > Run Script... on a result row's right-click menu runs an ExtendScript of yours once for each selected row.\n"
	L"\n"
	L"[The Kohaku+ tab]\n"
	L"- KFC adds a Kohaku+ tab to InDesign's own Find/Change dialog. While it is shown, Find in ... searches what its Find: says: Document, QR Code, Script Label, Spelling, Story, Table and, in a Japanese InDesign, Kenten, Ruby and Warichu.\n"
	L"- To save what the tab asks, press the save button on Find/Change's Query line.\n"
	L"\n"
	L"[Listing the open documents (Find: Document)]\n"
	L"- Find in ... lists the open documents.\n"
	L"- A click brings the document's window to the front; a document without a window is left without one.\n"
	L"- Open Hidden... opens a document you choose without a window.\n"
	L"\n"
	L"[Listing QR codes (Find: QR Code)]\n"
	L"- Find in ... lists the QR codes in the scope, with their kind (Web, Text, SMS, Email, vCard) and what they hold.\n"
	L"- Clicking a row goes to its QR code and selects it.\n"
	L"\n"
	L"[Listing and changing script labels (Find: Script Label)]\n"
	L"- Find in ... lists the page items in the scope whose script label holds Find Label: - empty, any label. Match: says how Find Label: is read: Text (Contains), its words anywhere in the label; Text (Whole Label), the label is all of it; GREP, a regular expression.\n"
	L"- Script Key: a name finds the labels a script put under that key (insertLabel - InDesign's Script Label panel does not show those); empty finds every key's.\n"
	L"\n"
	L"[Listing the words a wavy line is under (Find: Spelling)]\n"
	L"- Find in ... lists the words InDesign's Dynamic Spelling underlines in the scope.\n"
	L"- With Dynamic Spelling off (Edit > Spelling > Dynamic Spelling) nothing is searched.\n"
	L"\n"
	L"[Listing the stories (Find: Story)]\n"
	L"- Find in ... lists the stories in the scope, a row each.\n"
	L"- Clicking a row selects all of its story's text.\n"
	L"- Special Operations > Edit in Story Editor on a row opens its story in the Story Editor (a document opened without a window stays without one).\n"
	L"\n"
	L"[Finding tables by their table style (Find: Table)]\n"
	L"- Find in ... lists the tables in the scope that have its Find Table Style (and With Overrides Only), with their size and style - a + marks a table with overrides.\n"
	L"- Replace on a row (its right-click menu, or Return) gives its table the tab's Change to Table Style and Clear Overrides, as they stand then. One Ctrl+Z takes back the rows replaced together.\n"
	L"- Search Text in These Tables on the heading of those results searches the text in those tables with the query of the tab Find/Change is on (Text, GREP, Glyph or Transliterate), into new results at the bottom.\n"
	L"- List Rows of These Tables on the heading of those results lists the tables' rows, one row a table row (its header and footer rows named), into new results at the bottom. A click on a row selects its cells; its Special Operations > Apply Cell Style gives it a cell style.\n"
	L"\n"
	L"[Running saved queries in order (Run Saved Queries...)]\n"
	L"- Run Saved Queries... on the panel menu opens a dialog that lines up the queries saved in Find/Change and runs them in order.\n"
	L"- The left list (Saved Queries) shows the saved queries of the Text, GREP, Glyph, Object and Transliterate tabs. Colour queries are not supported.\n"
	L"- Add > adds a query to the end of the right list (Run Order). < Remove takes one out, Move Up / Move Down change the order, and Clear empties the right list.\n"
	L"- Find searches with the query selected in the right list. After searching, the selection moves down one row.\n"
	L"- Run (or Enter) runs the queries of the right list in order, each with InDesign's own Change All.\n"
	L"- After a Run the panel lists each place it wrote: what the place has been (cat → dog → kog) and the queries that wrote it. A row goes to its place; nothing is written from these rows.\n"
	L"- A query with nothing to change (QR Code, Story, Warichu and the like) is searched at its turn, and what it found is listed too.\n"
	L"- Save Order... saves the order to a file (.json), and Load Order... loads one.\n"
	L"- A double-click on a query in the left list loads that query into InDesign's own Find/Change dialog.\n"
	L"\n"
	L"[The Book panel (Remember Book Panel Placement)]\n"
	L"- ON: InDesign's own Book panel opens where it was and at the size it was (collapsed to icons, if it was) when it was last closed. This is recorded when a book is closed and when InDesign quits.\n"
	L"\n"
	L"[The application bar's search field (Link the Application Bar's Search Field to This Panel)]\n"
	L"- ON: the search field on InDesign's application bar (the Adobe Stock / Adobe Help one) shows the find string of Find/Change's Text tab, whichever tab Find/Change is on.\n"
	L"- Return in the field searches with KFC instead of opening a browser (Adobe Stock / Adobe Help): the field's text is put into Find/Change's Text tab first, and the search is a Text search, whichever tab Find/Change is on.";

const wchar_t* const kHowToJA =
	L"【免責】 どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。\n"
	L"\n"
	L"【検索（Find in …）】\n"
	L"・パネルメニューの Open Find/Change... で、ドキュメントが開いていなくても InDesign 本体の「検索と置換」ダイアログが開きます。\n"
	L"・InDesign 本体の検索の設定に従います。\n"
	L"・テキスト／正規表現／字形／オブジェクト／文字種変換／Kohaku+（KFC のタブ）で検索できます。\n"
	L"・カラーのタブは対象外です。\n"
	L"\n"
	L"【置換】\n"
	L"・InDesign 本体の置換の設定に従います。\n"
	L"・パネルメニューの Change All in Book（Book Scope が ON のとき）で、ブックのすべての章を本体の「すべてを置換」で置換し、置換した箇所を一覧にします（行は元の字と今の字）。パネルに一覧があれば、先に自動で外します。Ctrl+Z 1回で、外した一覧ごと全部戻ります。\n"
	L"\n"
	L"【検索範囲（Book Scope）】\n"
	L"・Book Scope: ON＝ブックパネルに表示しているブック（タブが手前にあるブック）の全ドキュメント。ブックパネルが見つからないときは、アクティブなブックを対象にします。\n"
	L"\n"
	L"【結果の見方】\n"
	L"・右クリックメニュー\n"
	L"・上下の矢印キーで、ツリー全体を巡回できます。\n"
	L"・結果の行をリターンで置換、Shift＋リターンで置換した後、一つ下の置換できる行に移動します。\n"
	L"・Ctrl＋リターンで、選んだ結果の行を一覧から外し、下の行に移動します。\n"
	L"・結果の見出しの右クリックの Narrow Down で、その結果を条件を1つ足して探し直し、一番下に新しい結果として出します。\n"
	L"・オブジェクト検索の結果の見出しの右クリックの Search Text in These Objects で、そのオブジェクトの中の文字（枠に入っている部分だけ。表・脚注・アンカー付きの枠・パス上の文字も）を、「検索と置換」で開いているタブ（テキスト／正規表現／字形／文字種変換）の条件で探し、一番下に新しい結果として出します。Clear from Results で外した行は探しません。\n"
	L"・Hide Previous Chapter: ON にすると、ジャンプ後に、検索したブックのジャンプ先以外の章のうち、保存するものが無いドキュメントウィンドウを閉じます。ブックに入っていないドキュメント、編集されたもの、一度も保存していないものは、そのまま残します。\n"
	L"・Open Without a Window（ブックの行の右クリックのトグル・最初は OFF）：ON にすると、ブックの結果の行をクリックしても章に窓を付けません。閉じた章は窓なしで開くだけで、何も選ばず、メッセージ欄にそう書きます。開いた章はあなたのものになり、Special Operations ▸ Edit in Story Editor でストーリーだけを開けます。Find: Document で見つけて Show・Close もできます。\n"
	L"\n"
	L"【複数の行をまとめて選ぶ・置換する】\n"
	L"・Ctrl＋クリックで行を足します。選んである行をもう一度 Ctrl＋クリックすると外れます。\n"
	L"・Shift＋クリックで、最初に選んだ行からその行までをまとめて選びます。Shift＋↓／Shift＋↑ で1行ずつ広げたり縮めたりできます。\n"
	L"・ストーリーやドキュメントの行の右クリックの Replace All Rows で、その下の行を全部まとめて置換します（閉じている行の下も）。置換できない行はそのままにして、そう言います。Ctrl+Z 1回で全部戻ります。\n"
	L"\n"
	L"【特殊操作（Special Operations）】\n"
	L"・結果の行の右クリックの Special Operations で、選んだ行に InDesign 本体の操作をします。\n"
	L"\n"
	L"【行にScriptを走らせる（Special Operations ▸ Run Script...）】\n"
	L"・結果の行の右クリックの Special Operations ▸ Run Script... で、あなたの ExtendScript を選んだ行ごとに1回ずつ走らせます。\n"
	L"\n"
	L"【Kohaku+ タブ】\n"
	L"・InDesign 本体の「検索と置換」に Kohaku+ タブが加わります。このタブを選んでいる間、Find in … は、タブの Find: で選んだものを探します（Document／QR Code／Script Label／Spelling／Story／Table／Kenten／Ruby／Warichu）。\n"
	L"・タブの条件を保存するには、検索と置換のクエリの行の保存ボタンを押します。\n"
	L"\n"
	L"【開いている文書を一覧にする（Find: Document）】\n"
	L"・Find in … で、開いている文書を一覧にします。\n"
	L"・行をクリックすると、その文書の窓を前に出します。窓の無い文書は窓なしのままにします。\n"
	L"・Open Hidden... で、選んだ文書を窓なしで開きます。\n"
	L"\n"
	L"【QR コードの一覧（Find: QR Code）】\n"
	L"・Find in … で、範囲の中の QR コードを、種類（Web・Text・SMS・Email・vCard）と中身つきで一覧にします。\n"
	L"・行をクリックすると、その QR コードへ飛んで選びます。\n"
	L"\n"
	L"【スクリプトラベルを一覧にする・書き換える（Find: Script Label）】\n"
	L"・Find in … で、範囲の中のページアイテムのうち、スクリプトラベルに Find Label: の字を含むものを一覧にします（空なら、ラベルのある物全部）。Match: で Find Label: の読み方を選びます。Text (Contains) はラベルのどこかにその字があるもの、Text (Whole Label) はラベル全体が同じもの、GREP は正規表現。\n"
	L"・Script Key: に名前を入れると、スクリプトがそのキーで付けたラベル（insertLabel・本体のスクリプトラベルのパネルには出ないもの）を探します。空なら全部のキー。\n"
	L"\n"
	L"【波線の語を一覧にする（Find: Spelling）】\n"
	L"・Find in … で、範囲の中で InDesign の自動スペルチェックが波線を引いている語を一覧にします。\n"
	L"・自動スペルチェック（編集＞スペルチェック＞自動スペルチェック）が切れていると探しません。\n"
	L"\n"
	L"【ストーリーを一覧にする（Find: Story）】\n"
	L"・Find in … で、範囲の中のストーリーを1つ1行で一覧にします。\n"
	L"・行をクリックすると、そのストーリーの本文の全部を選びます。\n"
	L"・行の Special Operations ▸ Edit in Story Editor で、ストーリーエディターで開きます（窓なしで開いた文書は窓なしのまま）。\n"
	L"\n"
	L"【表スタイルで表を探す（Find: Table）】\n"
	L"・Find in … で、範囲の中の表のうち Find Table Style（と With Overrides Only）に合うものを、大きさと表スタイルつきで一覧にします。+ は上書きのある表です。\n"
	L"・行の Replace（右クリックメニュー、または Return）で、その表にタブの Change to Table Style と Clear Overrides を、その時の設定で当てます。一緒に置換した行は Ctrl+Z 1回で戻ります。\n"
	L"・見出しの右クリックの Search Text in These Tables で、その表の中の文字を、本体の検索と置換のタブ（テキスト／正規表現／字形／文字種変換）の条件で探し、新しい結果を下に作ります。\n"
	L"・見出しの右クリックの List Rows of These Tables で、その表の行を1行ずつ並べた新しい結果を下に作ります（見出し行とフッター行はそう書きます）。行をクリックするとその行のセルを選びます。その行の Special Operations > Apply Cell Style でセルスタイルを当てられます。\n"
	L"\n"
	L"【保存したクエリを順に実行（Run Saved Queries...）】\n"
	L"・パネルメニューの Run Saved Queries... で、「検索と置換」で保存したクエリを並べて順に実行するダイアログが開きます。\n"
	L"・左（Saved Queries）に、保存したクエリ（テキスト／正規表現／字形／オブジェクト／文字種変換）が並びます。カラーのクエリは対象外です。\n"
	L"・Add > で右（Run Order）の最後に足します。< Remove で外す、Move Up / Move Down で順番を変える、Clear で右を空にします。\n"
	L"・Find で、右で選んでいるクエリで検索します。検索した後は、選択が1つ下に移ります。\n"
	L"・Run（または Enter）で、右の順に、各クエリを InDesign 本体の「すべてを置換」で実行します。\n"
	L"・Run の後、パネルに書いた場所の一覧が出ます。その場所が何から何になったか（cat → dog → kog）と、書いたクエリの名前が読めます。行から本文へ飛べます。行から書き込みはできません。\n"
	L"・Change 側が無いクエリ（QR Code・Story・割注など）は、その順番の時点で検索し、見つけた所も一覧に出します。\n"
	L"・Save Order... でファイル（.json）として保存し、Load Order... で読み込めます。\n"
	L"・左のクエリをダブルクリックすると、そのクエリを InDesign 本体の「検索と置換」に読み込みます。\n"
	L"\n"
	L"【ブックパネル（Remember Book Panel Placement）】\n"
	L"・ON にすると、InDesign 本体のブックパネルが、前回閉じたときの位置と大きさ（アイコン化していたらアイコンのまま）で開きます。これは、ブックを閉じたときと InDesign を終了したときに記録します。\n"
	L"\n"
	L"【アプリケーションバーの検索欄（Link the Application Bar's Search Field to This Panel）】\n"
	L"・ON にすると、InDesign 本体のアプリケーションバーの検索欄（Adobe Stock／Adobe Help の検索欄）に、「検索と置換」のテキストタブの検索文字列を表示します。どのタブを選んでいても同じです。\n"
	L"・検索欄で Enter を押すと、ブラウザ（Adobe Stock／Adobe Help）を開かずに、KFC で検索します。欄の文字を「検索と置換」のテキストタブに入れてから検索します。どのタブを選んでいても、テキストの検索になります。";

// RUN SCRIPT'S HELP... (the author, 11-06: "the // lines explain it now - put in a Help button and explain it there" -
// docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md RH1, RH3; UXP through app.doScript - the author: "if you want
// UXP, use it through DoScript"). Shown by the Run Script window's own script (KFCRowScript), in the UI language.
const wchar_t* const kRunScriptHelpEN =
	L"Run Script runs your ExtendScript (JavaScript) once for each row: the row you right-clicked, or every row selected together with it.\n"
	L"\n"
	L"[What the script is given]\n"
	L"- kfcResult: what the row stands for.\n"
	L"    a text match's row: its text (a Text; an insertion point for a match with no width; a match in a table's cell is that cell's text)\n"
	L"    an object row (the Object tab, QR Code, Script Label): the page item\n"
	L"    a Story row: the story (an internal story: its XmlStory)\n"
	L"    a Table row: the table; a row of a table row list: its Row; a cell's label row: the Cell\n"
	L"    a Document row (Find: Document): the Document\n"
	L"- kfcIndex: the row's place among them, from 0.\n"
	L"- kfcCount: how many rows it runs on.\n"
	L"\n"
	L"[Undo]\n"
	L"- One Ctrl+Z undoes everything the run did (its name: KFC Run Script).\n"
	L"\n"
	L"[Rows it does not run on]\n"
	L"- A row whose document is not open (a book's closed chapter), and a row that is no longer found, is counted, not run.\n"
	L"- An error on one row is counted and the other rows go on; the status line names the first error.\n"
	L"\n"
	L"[The window's buttons]\n"
	L"- Load... reads a script file into the box; Save As... writes the box to a file.\n"
	L"- The script run last is in the box again the next time.\n"
	L"\n"
	L"[Examples]\n"
	L"kfcResult.underline = true;                       // a text match: underline it\n"
	L"kfcResult.label = \"checked \" + (kfcIndex + 1);    // a page item: its script label\n"
	L"kfcResult.insertLabel(\"status\", \"done\");          // a page item, story or table: a label under a key of its own\n"
	L"if (kfcIndex == kfcCount - 1) alert(kfcCount + \" rows done\");   // once, on the last row\n"
	L"\n"
	L"[Using UXP]\n"
	L"Run Script runs ExtendScript. To use UXP, call it from the script with app.doScript:\n"
	L"var src = \"const { script } = require('uxp');\\n\" +\n"
	L"          \"script.setResult(String(Number(script.args[0]) * 2));\";\n"
	L"var answer = app.doScript(src, ScriptLanguage.UXPSCRIPT, [kfcIndex]);\n"
	L"- The UXP script's value comes back through require('uxp').script.setResult, as a string; its arguments are script.args (a number arrives as a string).\n"
	L"- What it changes is undone by the same one Ctrl+Z - except what it changes after an await: doScript returns at the first await, and the rest runs later, outside the run's undo step.";

const wchar_t* const kRunScriptHelpJA =
	L"Run Script は、行ごとに1回ずつ、ここに書いた ExtendScript（JavaScript）を走らせます。対象は、右クリックした行、またはその行と一緒に選んだ行すべてです。\n"
	L"\n"
	L"【台本に渡されるもの】\n"
	L"・kfcResult … その行が表すもの\n"
	L"　　文字の一致の行：その文字（Text。幅の無い一致は挿入点。表のセルの中の一致はそのセルの文字）\n"
	L"　　物の行（Object タブ・QR Code・Script Label）：そのページアイテム\n"
	L"　　Story の行：そのストーリー（内部のストーリーは XmlStory）\n"
	L"　　Table の行：その表／表の行の一覧の行：その Row／セルのラベルの行：その Cell\n"
	L"　　Document の行（Find: Document）：その文書（Document）\n"
	L"・kfcIndex … その行が何番目か（0 から）\n"
	L"・kfcCount … 走らせる行の数\n"
	L"\n"
	L"【取り消し】\n"
	L"・Ctrl＋Z を1回押すと、走らせた全部が戻ります（取り消しの名前は KFC Run Script）。\n"
	L"\n"
	L"【走らせない行】\n"
	L"・ドキュメントが開いていない行（ブックの閉じた章）と、見つからなくなった行は、数えるだけで走らせません。\n"
	L"・ある行でエラーが出ても、ほかの行は走ります。最初のエラーを状態行に書きます。\n"
	L"\n"
	L"【窓のボタン】\n"
	L"・Load... … 台本のファイルを欄に読み込みます。Save As... … 欄の台本をファイルに保存します。\n"
	L"・最後に走らせた台本は、次に開いたときも欄に入っています。\n"
	L"\n"
	L"【例】\n"
	L"kfcResult.underline = true;                       // 文字の一致：下線を付ける\n"
	L"kfcResult.label = \"checked \" + (kfcIndex + 1);    // ページアイテム：スクリプトラベルを書く\n"
	L"kfcResult.insertLabel(\"status\", \"done\");          // ページアイテム・ストーリー・表：キーを付けてラベルを書く\n"
	L"if (kfcIndex == kfcCount - 1) alert(kfcCount + \" 行が済みました\");   // 最後の行で一度だけ知らせる\n"
	L"\n"
	L"【UXP を使うとき】\n"
	L"Run Script は ExtendScript で走ります。UXP を使いたいときは、台本の中から app.doScript で呼びます。\n"
	L"var src = \"const { script } = require('uxp');\\n\" +\n"
	L"          \"script.setResult(String(Number(script.args[0]) * 2));\";\n"
	L"var answer = app.doScript(src, ScriptLanguage.UXPSCRIPT, [kfcIndex]);\n"
	L"・UXP 側の値は require('uxp').script.setResult で返します（文字列）。引数は script.args です（数も文字列で届きます）。\n"
	L"・UXP の中の変更も、同じ Ctrl＋Z 1回で戻ります。ただし await より後の変更は別です：doScript は最初の await の所で戻り、続きは後から走るので、取り消しの1段の外に出ます。";

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

int32 KFCHowTo::AppendRunScriptHelpJS(std::string& out, bool& outJapanese)
{
	// The UI language, as Show() asks it.
	outJapanese = (LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP);
	const wchar_t* const text = outJapanese ? kRunScriptHelpJA : kRunScriptHelpEN;
	AppendJSEscaped(out, text);
	return static_cast<int32>(std::wcslen(text));
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
