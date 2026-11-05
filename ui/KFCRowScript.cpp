//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Special Operations > Run Script... on rows - see KFCRowScript.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IExtendScriptUtils.h"		// RunScriptInEngine - the long form, the one that answers (KIDMCPRunScript.cpp's note)
#include "IScript.h"
#include "IScriptEngine.h"
#include "IScriptManager.h"
#include "IScriptUtils.h"			// GenerateSpecifier - a row's object as words its script resolves
#include "ISession.h"
#include "ITableModel.h"
#include "ITableModelList.h"
#include "ITableTextContainer.h"
#include "ITextModel.h"
#include "ITextScriptUtils.h"		// QueryTextObject - a match's text as a scripting object
#include "ITextStoryThread.h"

// General includes:
#include "JavaScriptID.h"			// kJavaScriptMgrBoss - ExtendScript's manager, whose specifiers the script resolves
#include "RangeData.h"
#include "ScriptData.h"
#include "TablesID.h"				// kTableModelBoss
#include "TableTypes.h"
#include "TextChar.h"				// kTextChar_Ellipse - a row's words cut
#include "Utils.h"

#include <cstdio>
#include <cstdlib>					// std::atoi - the script's answer read
#include <string>
#include <vector>

// Project includes:
#include "KFCRowScript.h"
#include "KFCDiag.h"				// the test build's fault switch run-script-source
#include "KFCModelAccess.h"			// KFCResults, KFCRuns

namespace
{

// The engine the run goes in - its own name, so the person's #targetengine sessions are not touched (RS10).
const char* const kEngineName = "KFC Run Script";
// The Edit menu's name of its one undo step (RS4).
const char* const kUndoName = "KFC Run Script";
// A row's words in a message are cut to this many characters.
const int32 kRowWordsMost = 24;

// The text's UTF-16 code units as the body of a JavaScript double-quoted string: the quote and the backslash escaped, a line break or a tab written out, every
// other code unit outside printable ASCII as \uXXXX (a surrogate pair as two) - the script pure ASCII whatever the text holds (KFCHowTo.cpp's way; memory
// doscript-japanese-encoding).
void AppendJSString(std::string& out, const PMString& words)
{
	PMString text(words);
	int32 units = 0;
	const UTF16TextChar* const buffer = text.GrabUTF16Buffer(&units);
	out += "\"";
	for (int32 i = 0; buffer != nil && i < units; ++i)
	{
		const UTF16TextChar ch = buffer[i];
		switch (ch)
		{
			case '"':	out += "\\\"";	break;
			case '\\':	out += "\\\\";	break;
			case '\n':	out += "\\n";	break;
			case '\r':	out += "\\r";	break;
			case '\t':	out += "\\t";	break;
			default:
				if (ch >= 0x20 && ch <= 0x7E)
					out += static_cast<char>(ch);
				else
				{
					char escaped[8];
					std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(ch));
					out += escaped;
				}
				break;
		}
	}
	out += "\"";
}

void AppendJSString(std::string& out, const char* ascii)
{
	PMString words(ascii);
	words.SetTranslatable(kFalse);
	AppendJSString(out, words);
}

// IS THE DOCUMENT OF THIS DATABASE OPEN NOW (RS5: a closed chapter's rows are counted, not run)? Only compared - a closed one's address is never read through.
bool DocIsOpen(IDataBase* db)
{
	if (db == nil)
		return false;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> documents((app != nil) ? app->QueryDocumentList() : nil);
	for (int32 d = 0; documents != nil && d < documents->GetDocCount(); ++d)
		if (::GetDataBase(documents->GetNthDoc(d)) == db)
			return true;
	return false;
}

// A scripting object's specifier in ExtendScript's own words (RS3) - empty when it could not be made.
std::string SpecifierOf(const EngineContext& context, IScript* object)
{
	if (object == nil)
		return std::string();
	PMString spec;
	if (Utils<IScriptUtils>()->GenerateSpecifier(context, object, spec) != kSuccess || spec.IsEmpty())
		return std::string();
	spec.SetTranslatable(kFalse);
	return std::string(spec.GetUTF8String());
}

// THE TABLE AND ITS CELL A TEXT POSITION STANDS IN (a table row list's row, a script label list's cell row - KFCJump.cpp's TableOfRowAt): nil when it is no cell's.
ITableModel* QueryTableOfCellAt(ITextModel* model, IDataBase* db, TextIndex at, GridAddress& outCell)
{
	if (model == nil || at < 0 || at >= model->TotalLength())
		return nil;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const UID dict = (thread != nil) ? thread->GetDictUID() : kInvalidUID;
	if (dict == kInvalidUID || !db->IsValidUID(dict) || db->GetClass(dict) != kTableModelBoss)
		return nil;
	InterfacePtr<ITableModel> table(db, dict, UseDefaultIID());
	if (table == nil)
		return nil;
	outCell = table->GetGridAddress(GridID(thread->GetDictKey()));
	return table.forget();
}

// THE TABLE WHOSE ANCHOR IS AT `at` in the story's own text (a Table list's row, a script label list's table row - the row stands on the table's character).
UID TableAnchoredAt(const UIDRef& storyRef, TextIndex at)
{
	InterfacePtr<ITableModelList> tables(storyRef, UseDefaultIID());
	for (int32 t = 0; tables != nil && t < tables->GetModelCount(); ++t)
	{
		InterfacePtr<ITableModel> table(tables->QueryNthModel(t));
		InterfacePtr<ITableTextContainer> text(table, UseDefaultIID());
		if (text != nil && text->GetAnchorTextIndex() == at)
			return ::GetUID(table);
	}
	return kInvalidUID;
}

// What one row runs on: a JavaScript expression that makes its object (resolve() of a specifier, and what follows it), or why there is none.
struct RowTarget
{
	std::string	expression;		// empty = not run
	PMString	words;			// the row's words, for a message
	int32		why;			// kWhyNone, or why it is not run
};
enum { kWhyNone = 0, kWhyNotOpen, kWhyGone, kWhyNoObject };

// "P1 cat" - the row's locator and what it draws, cut.
PMString RowWords(int32 chapter, int32 hit)
{
	KFCResultModel::RowDisplay row;
	PMString words;
	words.SetTranslatable(kFalse);
	if (!KFCResults()->GetHitRow(chapter, hit, row))
		return words;
	words.Append(row.locator);
	words.Append(" ");
	words.Append(row.runChain.IsEmpty() ? row.matchText : row.runChain);
	const WideString wide(words);
	if (wide.CharCount() > kRowWordsMost)
	{
		WideString cut;
		for (int32 i = 0; i < kRowWordsMost; ++i)
			cut.Append(wide.GetChar(i));
		words = PMString(cut);
		words.SetTranslatable(kFalse);
		words.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	return words;
}

// ONE ROW'S OBJECT AS AN EXPRESSION (RS3, RS5): its document open, its place found as a landing finds it (LocateRow - a text row's text focus has followed edits), its object's
// specifier made in ExtendScript's own context.
RowTarget TargetOf(const EngineContext& context, int32 chapter, int32 hit)
{
	RowTarget out;
	out.why = kWhyNone;
	out.words = RowWords(chapter, hit);
	UIDRef docRef;
	IDFile file;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	if (!KFCResults()->GetHitLocation(chapter, hit, docRef, file, story, start, end))
	{
		out.why = kWhyGone;
		return out;
	}
	IDataBase* const db = docRef.GetDataBase();
	if (!DocIsOpen(db))
	{
		out.why = kWhyNotOpen;
		return out;
	}
	std::string spec;
	std::string after;
	// AN OBJECT ROW: its page item, wherever it stands (a group's, an inline's).
	const UID item = KFCResults()->GetHitItem(chapter, hit);
	if (item != kInvalidUID)
	{
		if (KFCResults()->IsHitItemGone(chapter, hit) || !db->IsValidUID(item))
		{
			out.why = kWhyGone;
			return out;
		}
		InterfacePtr<IScript> object(db, item, UseDefaultIID());
		spec = SpecifierOf(context, object);
	}
	else
	{
		// A TEXT ROW, where it stands now.
		if (KFCRuns()->LocateRow(chapter, hit, docRef, start, end) == KFCResultModel::kRowNotFound || story == kInvalidUID || !db->IsValidUID(story))
		{
			out.why = kWhyGone;
			return out;
		}
		const UIDRef storyRef(db, story);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
		{
			out.why = kWhyGone;
			return out;
		}
		const int32 holder = KFCResults()->GetHitHolderKind(chapter, hit);
		InterfacePtr<IScript> storyScript(storyRef, UseDefaultIID());
		if (holder == kKFCHolderStory || holder == kKFCHolderInternalStory)
			spec = SpecifierOf(context, storyScript);		// a Story row: the story (an internal one's XmlStory)
		else if (KFCResults()->ListIsTables() || holder == kKFCHolderTable)
		{
			// A TABLE: its story's specifier and its own UID - GenerateSpecifier makes none for a table (2026-11-06), resolve() reads this one.
			const UID table = TableAnchoredAt(storyRef, start);
			const std::string storySpec(SpecifierOf(context, storyScript));
			if (table != kInvalidUID && !storySpec.empty())
				spec = storySpec + "/table[@id=" + std::to_string(table.Get()) + "]";
		}
		else if (KFCResults()->ListIsTableRows() || holder == kKFCHolderTableRow || holder == kKFCHolderCell)
		{
			// A TABLE ROW LIST'S ROW - its Row; A CELL'S LABEL ROW - its Cell: the table's specifier, then the row by its place, the cell by its name ("column:row").
			GridAddress cell;
			InterfacePtr<ITableModel> table(QueryTableOfCellAt(model, db, start, cell));
			const std::string storySpec(SpecifierOf(context, storyScript));
			if (table != nil && !storySpec.empty())
			{
				spec = storySpec + "/table[@id=" + std::to_string(::GetUID(table).Get()) + "]";
				if (holder == kKFCHolderCell)
					after = ".cells.itemByName(\"" + std::to_string(cell.col) + ":" + std::to_string(cell.row) + "\")";
				else
					after = ".rows[" + std::to_string(cell.row) + "]";
			}
		}
		else
		{
			// A MATCH: its text - a zero-width one's an insertion point.
			const RangeData range = (end > start) ? RangeData(start, end) : RangeData(start, RangeData::kLeanForward);
			InterfacePtr<IScript> text(Utils<ITextScriptUtils>()->QueryTextObject(context, model, range));
			spec = SpecifierOf(context, text);
		}
	}
	if (spec.empty())
	{
		out.why = kWhyNoObject;
		return out;
	}
	std::string expression("resolve(");
	PMString specWords;
	specWords.SetUTF8String(spec);
	AppendJSString(expression, specWords);
	expression += ")";
	expression += after;
	out.expression = expression;
	return out;
}

// THE WINDOW'S FIRST WORDS - the first time, before any script was run (RS2).
const char* const kSample =
	"// Run Script on Rows - KFC runs this once for each selected row.\n"
	"// kfcResult = what the row stands for: a match's text, an object row's page item,\n"
	"//             a Story row's story, a Table row's table (or its row, or a cell).\n"
	"// kfcIndex, kfcCount = this row's place among them (from 0), and how many.\n"
	"// One Ctrl+Z (Edit > Undo KFC Run Script) undoes the whole run.\n"
	"//\n"
	"// Take the // off a line to try it:\n"
	"// kfcResult.underline = true;                        // a match: underline it\n"
	"// kfcResult.label = \"checked \" + (kfcIndex + 1);     // a page item: its script label\n"
	"// $.writeln(kfcResult.constructor.name);             // what kind of object it is\n";

// THE SCRIPT THAT ASKS AND RUNS (RS2 - RS8): the rows' expressions and words, the window (or a test build's source), each row run inside one undo step. It answers
// "CANCEL", "SYNTAX<tab>why", or "DONE<tab>ran<tab>failed<tab>gone<tab>first error".
std::string ScriptFor(const std::vector<RowTarget>& targets, const PMString* testSource)
{
	std::string js("(function () {\nvar KFC_ROWS = [");
	bool first = true;
	for (size_t t = 0; t < targets.size(); ++t)
	{
		if (targets[t].expression.empty())
			continue;
		js += first ? "\n" : ",\n";
		js += "  function () { return " + targets[t].expression + "; }";
		first = false;
	}
	js += "\n];\nvar KFC_WORDS = [";
	first = true;
	for (size_t t = 0; t < targets.size(); ++t)
	{
		if (targets[t].expression.empty())
			continue;
		js += first ? "" : ", ";
		AppendJSString(js, targets[t].words);
		first = false;
	}
	js += "];\nvar KFC_TEST = ";
	if (testSource != nil)
		AppendJSString(js, *testSource);
	else
		js += "null";
	js += ";\nvar KFC_SAMPLE = ";
	AppendJSString(js, kSample);
	js +=
		";\n"
		"function kfcRead(f) { try { f.encoding = \"UTF-8\"; if (!f.open(\"r\")) return null; var s = f.read(); f.close(); return s; } catch (e) { return null; } }\n"
		"function kfcWrite(f, s) { try { f.encoding = \"UTF-8\"; if (!f.open(\"w\")) return false; f.write(s); f.close(); return true; } catch (e) { return false; } }\n"
		"var src = KFC_TEST;\n"
		"if (src === null) {\n"
		"  var keep = new File(app.scriptPreferences.scriptsFolder.parent.parent.fullName + \"/KFCRunScript.jsx\");\n"
		"  var last = keep.exists ? kfcRead(keep) : null;\n"
		"  var w = new Window(\"dialog\", \"Run Script on Rows\");\n"
		"  w.orientation = \"column\"; w.alignChildren = \"fill\";\n"
		"  w.add(\"statictext\", undefined, \"kfcResult = each selected row's object - \" + KFC_ROWS.length + \" row(s). One Ctrl+Z undoes the whole run.\");\n"
		"  var box = w.add(\"edittext\", undefined, (last !== null && last !== \"\") ? last : KFC_SAMPLE, { multiline: true, scrolling: true, wantReturn: true });\n"
		"  box.preferredSize = [620, 320];\n"
		"  var g = w.add(\"group\"); g.alignment = \"right\";\n"
		"  var load = g.add(\"button\", undefined, \"Load...\");\n"
		"  var save = g.add(\"button\", undefined, \"Save As...\");\n"
		"  g.add(\"button\", undefined, \"Cancel\", { name: \"cancel\" });\n"
		"  g.add(\"button\", undefined, \"Run\", { name: \"ok\" });\n"
		"  load.onClick = function () { var f = File.openDialog(\"Load a Script\", \"JavaScript:*.jsx;*.js,All files:*.*\"); if (f) { var s = kfcRead(f); if (s !== null) box.text = s; } };\n"
		"  save.onClick = function () { var f = File.saveDialog(\"Save the Script As\", \"JavaScript:*.jsx\"); if (f) kfcWrite(f, box.text); };\n"
		"  if (w.show() != 1) return \"CANCEL\";\n"
		"  src = box.text;\n"
		"  kfcWrite(keep, src);\n"
		"}\n"
		"var fn;\n"
		"try { fn = new Function(\"kfcResult\", \"kfcIndex\", \"kfcCount\", src); } catch (e) { return \"SYNTAX\\t\" + e; }\n"
		"var ran = 0, failed = 0, gone = 0, firstError = \"\";\n"
		"app.doScript(function () {\n"
		"  for (var i = 0; i < KFC_ROWS.length; i++) {\n"
		"    var r = null;\n"
		"    try { r = KFC_ROWS[i](); } catch (e) { r = null; }\n"
		"    var valid = (r !== null && r !== undefined);\n"
		"    if (valid) { try { valid = r.isValid; } catch (ev) { valid = true; } }\n"
		"    if (!valid) { gone++; if (firstError === \"\") firstError = KFC_WORDS[i] + \": not there now\"; continue; }\n"
		"    try { fn(r, i, KFC_ROWS.length); ran++; }\n"
		"    catch (e2) { failed++; if (firstError === \"\") firstError = KFC_WORDS[i] + \": \" + ((e2 && e2.name) ? e2.name + \": \" : \"\") + ((e2 && e2.message) ? e2.message : String(e2)); }\n"
		"  }\n"
		"}, ScriptLanguage.JAVASCRIPT, undefined, UndoModes.ENTIRE_SCRIPT, ";
	AppendJSString(js, kUndoName);
	js +=
		");\n"
		"return \"DONE\\t\" + ran + \"\\t\" + failed + \"\\t\" + gone + \"\\t\" + firstError;\n"
		"})();\n";
	return js;
}

// "1 row(s)" in the status line's way.
void AppendRows(PMString& io, int32 n)
{
	io.AppendNumber(n);
	io.Append(" row(s)");
}

}	// anonymous namespace

bool KFCRowScript::Run(const KFCRowsByChapter& rows, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// ExtendScript's own context: the specifiers are in its words (KIDMCP's KIDMCPCmdWatch.cpp RefOfLabelled - the same three steps).
	Utils<IScriptUtils> scriptUtils;
	InterfacePtr<IScriptManager> manager(scriptUtils.Exists() ? scriptUtils->QueryScriptManager(kJavaScriptMgrBoss) : nil);
	InterfacePtr<IScriptEngine> engine((manager != nil) ? manager->QueryDefaultEngine() : nil);
	Utils<IExtendScriptUtils> esUtils;
	if (engine == nil || !esUtils.Exists() || !Utils<ITextScriptUtils>().Exists())
	{
		outStatus.Append("Run Script: ExtendScript could not be reached - nothing was run.");
		return false;
	}
	const EngineContext context = engine->GetRequestContext();
	std::vector<RowTarget> targets;
	int32 runnable = 0, notOpen = 0, gone = 0;
	for (size_t c = 0; c < rows.size(); ++c)
		for (size_t h = 0; h < rows[c].hits.size(); ++h)
		{
			targets.push_back(TargetOf(context, rows[c].chapter, rows[c].hits[h]));
			const int32 why = targets.back().why;
			if (why == kWhyNone)
				++runnable;
			else if (why == kWhyNotOpen)
				++notOpen;
			else
				++gone;
		}
	if (runnable == 0)
	{
		outStatus.Append("Run Script: nothing to run on - ");
		if (notOpen > 0)
		{
			AppendRows(outStatus, notOpen);
			outStatus.Append(notOpen == static_cast<int32>(targets.size()) ? " whose documents are not open - open them and run again." : " whose documents are not open");
		}
		if (gone > 0)
		{
			if (notOpen > 0)
				outStatus.Append(", ");
			AppendRows(outStatus, gone);
			outStatus.Append(" not found - search again.");
		}
		return false;
	}
	// THE SCRIPT ITSELF: a test build's source, or the window's (RS9).
	PMString testSource;
	bool test = false;
#ifdef KFC_DIAG
	if (KFC_DIAG_FAULT("run-script-source"))
	{
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("run-script-source", lines);
		std::string joined;
		for (size_t n = 0; n < lines.size(); ++n)
			joined += (n > 0 ? "\n" : "") + lines[n];
		testSource.SetUTF8String(joined);
		test = true;
	}
#endif
	PMString scriptText;
	scriptText.SetUTF8String(ScriptFor(targets, test ? &testSource : nil));
	scriptText.SetTranslatable(kFalse);
	ScriptData script;
	script.SetPMString(scriptText);
	ScriptRecordData arguments;		// (nothing passed: the script carries every row)
	ScriptData result;
	PMString error;
	PMString engineName(kEngineName);
	engineName.SetTranslatable(kFalse);
	const ErrorCode ran = esUtils->RunScriptInEngine(engineName, script, arguments, result, error, kFalse /*showErrorAlert*/, kFalse /*invokeDebugger*/);
	error.SetTranslatable(kFalse);
	PMString answer;
	(void)result.GetPMString(answer);
	answer.SetTranslatable(kFalse);
	KFC_DIAG_LOG("RUNSCRIPT rows=%d runnable=%d notopen=%d gone=%d err=%d answer=%s error=%s", static_cast<int>(targets.size()), static_cast<int>(runnable),
		static_cast<int>(notOpen), static_cast<int>(gone), static_cast<int>(ran), answer.GetUTF8String().c_str(), error.GetUTF8String().c_str());
	if (ran != kSuccess || !error.IsEmpty())
	{
		outStatus.Append("Run Script: the script stopped - ");
		outStatus.Append(error.IsEmpty() ? PMString("no reason given") : error);
		return false;
	}
	const std::string said(answer.GetUTF8String());
	if (said == "CANCEL")
	{
		outStatus.Append("Run Script: cancelled - nothing was run.");
		return false;
	}
	if (said.compare(0, 7, "SYNTAX\t") == 0)
	{
		PMString why;
		why.SetUTF8String(said.substr(7));
		outStatus.Append("Run Script: the script did not compile - ");
		outStatus.Append(why);
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	// "DONE<tab>ran<tab>failed<tab>gone<tab>first error"
	int32 done = 0, failed = 0, goneNow = 0;
	std::string firstError;
	if (said.compare(0, 5, "DONE\t") == 0)
	{
		std::vector<std::string> parts;
		size_t from = 5;
		for (int32 k = 0; k < 3; ++k)
		{
			const size_t tab = said.find('\t', from);
			parts.push_back(said.substr(from, (tab == std::string::npos) ? std::string::npos : tab - from));
			from = (tab == std::string::npos) ? said.size() : tab + 1;
		}
		done = std::atoi(parts[0].c_str());
		failed = std::atoi(parts[1].c_str());
		goneNow = std::atoi(parts[2].c_str());
		firstError = said.substr(from);
	}
	outStatus.Append("Run Script: ran on ");
	outStatus.AppendNumber(done);
	outStatus.Append(" of ");
	AppendRows(outStatus, static_cast<int32>(targets.size()));
	if (failed > 0)
	{
		outStatus.Append("; ");
		outStatus.AppendNumber(failed);
		outStatus.Append(failed == 1 ? " error" : " errors");
	}
	if (goneNow + gone > 0)
	{
		outStatus.Append("; ");
		AppendRows(outStatus, goneNow + gone);
		outStatus.Append(" not found");
	}
	if (notOpen > 0)
	{
		outStatus.Append("; ");
		AppendRows(outStatus, notOpen);
		outStatus.Append(" whose documents are not open");
	}
	if (!firstError.empty())
	{
		PMString first;
		first.SetUTF8String(firstError);
		outStatus.Append(" - ");
		outStatus.Append(first);
	}
	else
		outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return done > 0;
}

// End, KFCRowScript.cpp.
