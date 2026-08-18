//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  "Save Results..." on the panel's flyout menu (2026-08-03): ask for a target file with the stock
//  save chooser and write the current result set as tab-separated text - one line per hit, every
//  stored hit including those past the panel's display cap.
//
//  The text is built AT SAVE TIME (KBSResultModel::BuildReportText), so the file describes the result
//  set as it stands at that moment - a replace that has since turned the list into a report of what it
//  did is written as that report, not as the search it started from.
//
//  It is written as UTF-8 with a BOM and CRLF line ends, so Notepad and Excel both open it correctly
//  (the rows routinely carry Japanese text, so the encoding must be explicit).
//
//  Only what went WRONG reaches the panel's status line. A save that worked says nothing - the file is
//  where the user put it - and a cancelled chooser does nothing, silently.
//
//  It was written FROM KESCL's KESCLReportSave.cpp, step for step, and the two are still the same
//  shape. *They are no longer the same code, so read that one as the origin rather than as the
//  reference (counted 2026-08-11): this file has since gained the run guard's front door, a stream
//  state check on the newly created stream, and a length cap on the name parts.
//  *What that stream check buys is SMALLER than this note claimed until 2026-08-12. KESCL's
//   KESCLReportPanel::SaveReportAsText does test the new stream for nil alone - but it reads
//   GetStreamState after its Flush just as this file does, and says so on failure, so a write that
//   goes wrong IS reported there. The difference is only whether a stream that arrives broken is
//   written to before anyone notices, not whether the user is told. Two earlier versions of this
//   note said KESCL "reports it as saved", and that was never true: the claim rode through a
//   re-check that corrected the LINE REFERENCE (it had drifted three lines) without opening the
//   file the reference pointed at. Named rather than numbered now - a line in ANOTHER repository
//   cannot be kept honest from this one - and the claim itself is what needed the checking.
//  *Smaller AGAIN as of 2026-08-18 (found by KESCM's bug recheck B10). The state check was
//   justified further down by pointing at SnipRunScriptProvider.cpp:488 as "the way the SDK's own
//   writer does" - but that snippet calls CreateFileStreamWriteLazy, a DIFFERENT function whose
//   lazy open cannot report an unwritable path as nil, leaving the state as its only check. What
//   StreamUtil.h:152 documents for the non-lazy call is exactly the nil test KESCL and KESCM use.
//   So this file is not more careful than they are: it carries one extra test that costs nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFindChangeOptions.h"	// the SearchMode values the results were recorded with
#include "IPMStream.h"

// General includes:
#include "PMString.h"
#include "TextCharBuffer.h"		// IsHighSurrogate - never cut a name part through a surrogate pair
#include "StreamUtil.h"
#include "SDKFileHelper.h"		// SDKFileSaveChooser (sdksamples/common)

#include <string>

// Project includes:
#include "KBSID.h"
#include "KBSResultTree.h"
#include "KBSResultModel.h"
#include "KBSRunGuard.h"

namespace
{

// One-off status line, spelled the way every other one-liner in this plug-in is.
void ShowStatus(const char* text)
{
	PMString message(text);
	message.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(message);
}

// A PMString as wide characters, so the file-name work below can walk it a character at a time.
// wchar_t is UTF-16 on Windows, which is what a PMString hands back - the same cast KESCL's report
// save uses.
std::wstring ToWide(const PMString& s)
{
	PMString copy(s);
	const UTF16TextChar* const buffer = copy.GrabUTF16Buffer(nil);
	if (buffer == nil)
		return std::wstring();
	return std::wstring(reinterpret_cast<const wchar_t*>(buffer));
}

// A part of the suggested file name is capped at this many characters: a GREP query is not a file
// name, and a 200-character one is not usable as one.
const size_t kMaxNamePart = 20;

// Make one name part safe for a Windows file name: the nine reserved characters (\ / : * ? " < > |)
// become '-'. Everything else - Japanese, spaces - passes through.
std::wstring SanitizeForFileName(const std::wstring& part)
{
	std::wstring out(part);
	for (size_t i = 0; i < out.size(); ++i)
	{
		const wchar_t c = out[i];
		if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?'
			|| c == L'"' || c == L'<' || c == L'>' || c == L'|')
		{
			out[i] = L'-';
		}
	}
	if (out.size() > kMaxNamePart)
	{
		// Never through the middle of a surrogate PAIR. wchar_t is a UTF-16 code UNIT on Windows, so
		// a character outside the BMP - a rare kanji, an emoji in a document name - is two of them,
		// and a cut between the two leaves a lone high surrogate in the name the save chooser shows.
		// Backing up one unit is enough: the pair is the only multi-unit sequence UTF-16 has.
		// The TEST is the SDK's own - IsHighSurrogate, TextCharBuffer.h:32, the same one the SDK's
		// own character-at-a-time walk over a WideString uses (TextCharBuffer::RemoveFirst, :90-95).
		// It is only the CUTTING that has nothing official to borrow: PMString::Truncate counts the
		// same platform units, and StringUtils::PMEllipsizeString measures WIDTH and wants a
		// graphics context.
		size_t cut = kMaxNamePart;
		if (cut > 0 && IsHighSurrogate(static_cast<UTF16TextChar>(out[cut - 1])))
			--cut;
		out.erase(cut);
	}
	return out;
}

// What was DONE, and it leads the file name (user's call, 2026-08-03): "FindText", "FindGrep",
// "ChangeText", "MissingGlyphs", "Overset". A folder of these then sorts by the thing that tells them
// apart, and "which of these was the GREP one" is answerable without opening any of them.
std::wstring ActionNamePart()
{
	switch (KBSResultModel::GetResultKind())
	{
		case KBSResultModel::kResultMissingGlyph:
			return std::wstring(L"MissingGlyphs");

		case KBSResultModel::kResultOverset:
			return std::wstring(L"Overset");

		case KBSResultModel::kResultFindChange:
			break;
	}

	// Find or Change: a replace's aftermath is a report of what was CHANGED, which is a different
	// thing to have saved than the search that found it.
	std::wstring name(KBSResultModel::IsShowingReplaceOutcome() ? L"Change" : L"Find");

	// ...and the tab it ran on. Taken from the mode recorded ON THE RESULTS, not from the dialog: the
	// user can switch tabs between the search and the save. Spelled "Grep" rather than the dialog's
	// "GREP" because this is a file name, not a label.
	switch (KBSResultModel::GetSearchMode())
	{
		case IFindChangeOptions::kGrepSearch:	name += L"Grep";	break;
		case IFindChangeOptions::kGlyphSearch:	name += L"Glyph";	break;
		case IFindChangeOptions::kTransliterateSearch:	name += L"Transliterate";	break;
		default:								name += L"Text";	break;
	}
	return name;
}

// The suggested file name (user's call, 2026-08-03):
//
//     KohakuFindChangeReport_<book or document>_<what was done>.txt
//
// e.g. "KohakuFindChangeReport_ch1_FindText.txt", "KohakuFindChangeReport_savetest_FindGrep.txt",
// "KohakuFindChangeReport_glyphbook_MissingGlyphs.txt".
//
// The plug-in's own stem leads, so these files are recognisable wherever they end up; then what was
// searched, since that is what the user is working ON; then what was done to it, which tells two
// saves of the same document apart. The query itself is NOT in the name - it can be anything at all,
// including a GREP pattern made of punctuation - and the heading inside the file names it exactly.
// The scope's extension is dropped ("ch1.indd" -> "ch1"), and a part that cannot be named is skipped,
// so a bare "KohakuFindChangeReport.txt" is the worst case.
PMString BuildSuggestedFileName()
{
	// What was run over.
	PMString scopeName;
	if (KBSResultModel::IsFromBook())
	{
		scopeName = KBSResultModel::GetBookName();
	}
	else
	{
		PMString chapterName;
		int32 chapterHits = 0;
		if (KBSResultModel::GetChapterDisplay(0, chapterName, chapterHits))
			scopeName = chapterName;
	}

	std::wstring scope = ToWide(scopeName);
	const size_t dot = scope.find_last_of(L'.');
	if (dot != std::wstring::npos && dot > 0)
		scope.erase(dot);

	const std::wstring action = ActionNamePart();

	std::wstring name(L"KohakuFindChangeReport");
	const std::wstring* const parts[] = { &scope, &action };
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
	{
		if (parts[i]->empty())
			continue;
		const std::wstring safe = SanitizeForFileName(*parts[i]);
		if (safe.empty())
			continue;
		name += L'_';
		name += safe;
	}
	name += L".txt";

	PMString result;
	result.SetTranslatable(kFalse);
	result.AppendW(reinterpret_cast<const UTF16TextChar*>(name.c_str()));
	return result;
}

} // anonymous namespace

void KBSResultTree::SaveResultsAsText()
{
	// A run of ours is standing behind its modal progress bar, and that bar pumps events - so this can
	// be reached from inside one. The model is being cleared and appended to as it runs; reading it
	// mid-flight would write a file describing a result set that never existed. The flyout greys this
	// item out in that state, so this is the script route's backstop, the way every other command here
	// has one.
	if (KBSRunGuard::IsAnyRunning())
	{
		PMString busy(KBSRunGuard::BusyMessage());
		busy.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(busy);
		return;
	}

	// Belt and braces: the flyout item is greyed out while there is nothing to write.
	if (KBSResultModel::GetTotalHitCount() == 0)
	{
		ShowStatus("No results to save yet.");
		return;
	}

	// The RUN's own summary sentence goes into the heading - not the panel's status line, which
	// anything may have overwritten since (a tick writes "P1(2)  checked" there; that was the
	// shipped heading until 2026-08-09). Every run records its sentence as it commits its results
	// (KBSResultModel::NoteRunSummary), so with rows standing there is always one; the status line
	// stands in only if a future path ever leaves results without recording.
	PMString summary = KBSResultModel::GetRunSummary();
	if (summary.IsEmpty())
		KBSResultTree::GetLastStatus(summary);

	PMString report;
	KBSResultModel::BuildReportText(summary, report);
	if (report.IsEmpty())
		return;

	// Stock save chooser (sdksamples/common). Title and suggested name are fixed English text like all
	// KBS UI; 'TEXT'/'CWIE' are the classic Mac type/creator the SDK's own text-writing snippets pass -
	// meaningless on Windows, but the API wants them.
	SDKFileSaveChooser chooser;
	PMString title("Save Results");
	title.SetTranslatable(kFalse);
	chooser.SetTitle(title);
	chooser.SetFilename(BuildSuggestedFileName());
	PMString filterName("Text file(txt)");
	filterName.SetTranslatable(kFalse);
	chooser.AddFilter('CWIE', 'TEXT', "txt", filterName);
	chooser.ShowDialog();
	if (!chooser.IsChosen())
		return;

	// UTF-8 with a BOM, '\n' -> '\r\n': the report is built with bare '\n', and a BOM-less file full of
	// Japanese text is exactly what Notepad and Excel guess wrong.
	const std::string utf8 = report.GetUTF8String();
	std::string bytes;
	bytes.reserve(utf8.size() + utf8.size() / 8 + 3);
	bytes += "\xEF\xBB\xBF";
	for (std::string::const_iterator it = utf8.begin(); it != utf8.end(); ++it)
	{
		if (*it == '\n')
			bytes += "\r\n";
		else
			bytes += *it;
	}

	// The stream comes back OPEN - IPMStream.h:359-365 states that Open "gets called for you by the
	// StreamUtils functions" - so its state can be asked for straight away, rather than after the
	// bytes have gone in. The nil half of the test is the DOCUMENTED one: StreamUtil.h:152 promises
	// "the new stream, or nil if the file couldn't be opened". The state half is belt and braces on
	// top of it and costs nothing.
	//
	// *This note used to justify the state check by saying it is "the way the SDK's own writer does
	//  (SnipRunScriptProvider.cpp:488)". That reference was measured on 2026-08-18 (during KESCM's
	//  bug recheck B10) and the justification does not hold: that snippet calls
	//  CreateFileStreamWriteLazy - the LAZY variant, which does not open the file on creation, so it
	//  cannot return nil for an unwritable path and the state check is the ONLY check available to
	//  it. Two functions whose names start the same do not share a contract. KESCL and KESCM test
	//  nil alone; that is the documented contract being followed, not a check they forgot.
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(
		chooser.GetIDFile(), kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil || stream->GetStreamState() != kStreamStateGood)
	{
		ShowStatus("Could not create the file. Is the folder writable?");
		return;
	}
	stream->XferByte(reinterpret_cast<uchar*>(&bytes[0]), static_cast<int32>(bytes.size()));
	// State read AFTER Flush: XferByte may only buffer, so a write that dies on the way to disk (full
	// disk, yanked drive) can surface at Flush - checking before it would report that as "saved".
	stream->Flush();
	const bool failed = (stream->GetStreamState() == kStreamStateFailure);
	stream->Close();

	// Only the failure is said. A save that worked shows a file where the user put it, so the status
	// line has nothing to add - silence reads as success here, the way a search's does.
	if (failed)
		ShowStatus("Could not write the report file.");
}

// End, KBSReportSave.cpp.
