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
//  where the user put it - and a cancelled chooser does nothing, silently. This is KESCL's
//  KESCLReportSave.cpp, which this file follows step for step.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"

// General includes:
#include "PMString.h"
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
		out.erase(kMaxNamePart);
	return out;
}

// What was asked for, as a file-name part. A scan has no query, so it says which scan it was -
// otherwise two scans of the same book would suggest the same name.
std::wstring QueryNamePart()
{
	switch (KBSResultModel::GetResultKind())
	{
		case KBSResultModel::kResultMissingGlyph:
			return std::wstring(L"MissingGlyphs");

		case KBSResultModel::kResultOverset:
			return std::wstring(L"Overset");

		case KBSResultModel::kResultFindChange:
		{
			// The recorded query carries its tab in brackets ("cat  (Text)"), which is heading text,
			// not name text: cut it back to the query itself.
			std::wstring query = ToWide(KBSResultModel::GetQueryText());
			const size_t bracket = query.find(L"  (");
			if (bracket != std::wstring::npos)
				query.erase(bracket);
			return query;
		}
	}
	return std::wstring();
}

// The suggested file name, built from what the results describe:
// "KohakuFindChangeReport_<book or document>_<query>.txt", e.g.
// "KohakuFindChangeReport_savetest_KOHAKU.txt". The scope's extension is dropped ("ch1.indd" ->
// "ch1"); empty parts are skipped, so a bare "KohakuFindChangeReport.txt" is the worst case.
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

	const std::wstring what = QueryNamePart();

	std::wstring name(L"KohakuFindChangeReport");
	const std::wstring* const parts[] = { &scope, &what };
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

	// The panel's own summary sentence goes into the heading - see BuildReportText.
	PMString summary;
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

	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(
		chooser.GetIDFile(), kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil)
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
