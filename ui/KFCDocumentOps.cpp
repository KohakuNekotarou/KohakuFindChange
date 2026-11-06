//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A Find: Document row's Special Operations and the Document face's Open Hidden... - see KFCDocumentOps.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"			// QueryDocumentList - is a chosen file open already
#include "IDataBase.h"
#include "IDocFileHandler.h"		// Close / Save and their Can... - InDesign's own file doors
#include "IDocument.h"
#include "IDocumentCommands.h"		// Open - showInWindow kFalse (Open Hidden...)
#include "IDocumentList.h"
#include "IDocumentUtils.h"			// QueryDocFileHandler
#include "IOpenFileCmdData.h"		// kOpenDefault / kUseLockFile
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"				// PMGetGlobalErrorCode / PMSetGlobalErrorCode - a failed open's or save's error, said and cleared
#include "FileUtils.h"				// IsEqual, IDFileToPMString, PMStringToIDFile
#include "PersistUtils.h"			// ::GetDataBase
#include "SDKFileHelper.h"			// SDKFileOpenChooser - Open Hidden...'s file
#include "Utils.h"

#include <set>
#include <string>
#include <vector>

// Project includes:
#include "KFCDocumentOps.h"
#include "KFCUIID.h"				// kKFCDocumentOpenHiddenTitleKey
#include "KFCModelAccess.h"			// KFCRuns, KFCResults
#include "IKFCUIServices.h"			// CountDocWindows, OpenLayoutWindow - this half's own services
#include "KFCDiag.h"

namespace
{

// THE ROW'S DOCUMENT OPEN NOW, asked by its file (never the address the results hold - memory uidref-reuse-after-close; the D20-a rule).
UIDRef DocOfChapter(int32 chapterIdx)
{
	return KFCRuns()->ChapterDocNow(chapterIdx);
}

// Its layout windows (Show's question - a Story Editor's window shows no layout).
int32 LayoutWindows(const UIDRef& docRef)
{
	int32 layout = 0, other = 0;
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	if (ui != nil)
		ui->CountDocWindows(docRef, layout, other);
	return layout;
}

bool CanDoDoc(KFCDocumentOps::Op op, const UIDRef& docRef)
{
	if (docRef.GetDataBase() == nil)
		return false;
	if (op == KFCDocumentOps::kShow)
		return LayoutWindows(docRef) == 0;
	InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (handler == nil)
		return false;
	if (op == KFCDocumentOps::kClose)
		return handler->CanClose(docRef) != kFalse;
	return handler->CanSave(docRef) != kFalse;
}

const char* OpName(KFCDocumentOps::Op op)
{
	return (op == KFCDocumentOps::kShow) ? "Show" : (op == KFCDocumentOps::kClose) ? "Close" : "Save";
}

// "1 document(s)" in the status line's way.
void AppendDocs(PMString& io, int32 n)
{
	io.AppendNumber(n);
	io.Append(" document(s)");
}

// A file's name, off its path.
PMString NameOf(const IDFile& file)
{
	PMString path;
	FileUtils::IDFileToPMString(file, path);
	const std::string p(path.GetUTF8String());
	const size_t slash = p.find_last_of("\\/");
	PMString name;
	name.SetUTF8String((slash == std::string::npos) ? p : p.substr(slash + 1));
	name.SetTranslatable(kFalse);
	return name;
}

// The open document in this file, or gNull - opens nothing.
UIDRef OpenDocOfFile(const IDFile& file)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docs(app != nil ? app->QueryDocumentList() : nil);
	if (docs == nil)
		return UIDRef::gNull;
	for (int32 i = 0; i < docs->GetDocCount(); ++i)
	{
		IDocument* const doc = docs->GetNthDoc(i);
		IDataBase* const db = (doc != nil) ? ::GetDataBase(doc) : nil;
		const IDFile* const open = (db != nil) ? db->GetSysFile() : nil;
		if (open != nil && FileUtils::IsEqual(*open, file))
			return ::GetUIDRef(doc);
	}
	return UIDRef::gNull;
}

}	// anonymous namespace

bool KFCDocumentOps::CanDo(Op op, int32 chapterIdx, int32 hitIdx)
{
	if (!KFCResults()->ListIsDocuments() || KFCResults()->GetHitHolderKind(chapterIdx, hitIdx) != kKFCHolderDocument)
		return false;
	return CanDoDoc(op, DocOfChapter(chapterIdx));
}

bool KFCDocumentOps::Do(Op op, const KFCRowsByChapter& rows, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	int32 done = 0, notOpen = 0, cannot = 0, failed = 0, total = 0;
	std::set<int32> seen;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		const int32 chapter = rows[c].chapter;
		if (seen.count(chapter) != 0)
			continue;
		bool documentRow = false;
		for (size_t h = 0; h < rows[c].hits.size() && !documentRow; ++h)
			documentRow = (KFCResults()->GetHitHolderKind(chapter, rows[c].hits[h]) == kKFCHolderDocument);
		if (!documentRow)
			continue;
		seen.insert(chapter);
		++total;
		const UIDRef docRef = DocOfChapter(chapter);
		if (docRef.GetDataBase() == nil)
		{
			++notOpen;
			continue;
		}
		if (!CanDoDoc(op, docRef))
		{
			++cannot;
			continue;
		}
		if (op == kShow)
		{
			InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
			if (ui != nil && ui->OpenLayoutWindow(docRef))
				++done;
			else
				++failed;
			continue;
		}
		InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
		if (handler == nil)
		{
			++failed;
			continue;
		}
		// InDesign's own doors, with their own questions (kFullUI): a document holding changes asks "save?" on Close; one never saved asks for a file on Save.
		// Close is SCHEDULED - the official way from a UI's call (memory book-chapter-windowless-open): it closes once this press is over, its rows going
		// off the list then (LIFE-01). No interface on the document is held here (memory close-doc-while-holding-interface).
		if (op == kClose)
			handler->Close(docRef, kFullUI, kTrue, IDocFileHandler::kSchedule);
		else
			handler->Save(docRef, kFullUI);
		const ErrorCode said = ErrorUtils::PMGetGlobalErrorCode();
		if (said != kSuccess && op != kClose)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// (InDesign's own dialog told the person; the press is over)
			++failed;
		}
		else
			++done;
	}
	KFC_DIAG_LOG("DOCOP %s rows=%d done=%d notopen=%d cannot=%d failed=%d", OpName(op), static_cast<int>(total), static_cast<int>(done), static_cast<int>(notOpen),
		static_cast<int>(cannot), static_cast<int>(failed));
	// "Show: opened a window on 1 of 2 document(s); 1 has one already." - what was not done, each said.
	outStatus.Append(OpName(op));
	outStatus.Append(": ");
	outStatus.Append((op == kShow) ? "opened a window on " : (op == kClose) ? "closing " : "saved ");
	outStatus.AppendNumber(done);
	outStatus.Append(" of ");
	AppendDocs(outStatus, total);
	if (cannot > 0)
	{
		outStatus.Append("; ");
		outStatus.AppendNumber(cannot);
		outStatus.Append((op == kShow) ? " with a window already" : (op == kSave) ? " with nothing to save" : " that cannot be closed");
	}
	if (failed > 0)
	{
		outStatus.Append("; ");
		outStatus.AppendNumber(failed);
		outStatus.Append(" not done");
	}
	if (notOpen > 0)
	{
		outStatus.Append("; ");
		outStatus.AppendNumber(notOpen);
		outStatus.Append(" not open any more");
	}
	outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return done > 0;
}

bool KFCDocumentOps::OpenHidden(PMString& outSaid)
{
	outSaid.Clear();
	outSaid.SetTranslatable(kFalse);
	// NOT UNDER A RUN OF OURS (the face's other buttons' door - an open runs the message loop; a script's press reaches here past any grey).
	if (KFCRuns()->IsAnyRunning())
	{
		outSaid.Append(KFCRuns()->BusyMessage());
		outSaid.SetTranslatable(kFalse);
		return false;
	}
	std::vector<IDFile> files;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch open-hidden-file: its lines the files, the dialog not shown.
	if (KFC_DIAG_FAULT("open-hidden-file"))
	{
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("open-hidden-file", lines);
		for (size_t n = 0; n < lines.size(); ++n)
			if (!lines[n].empty() && lines[n] != "on")
			{
				PMString path;
				path.SetUTF8String(lines[n]);
				IDFile file;
				FileUtils::PMStringToIDFile(path, file);
				files.push_back(file);
			}
	}
#endif
	if (files.empty())
	{
		SDKFileOpenChooser chooser;
		chooser.SetTitle(PMString(kKFCDocumentOpenHiddenTitleKey));		// "Open Hidden"
		PMString kind("InDesign Document");
		kind.SetTranslatable(kFalse);
		chooser.AddFilter('IDd5', PMString("indd"), kind);
		chooser.ShowDialog();
		if (!chooser.IsChosen())
		{
			outSaid.Append("Open Hidden: cancelled - nothing was opened.");
			return false;
		}
		files.push_back(chooser.GetIDFile());
	}
	int32 opened = 0, already = 0, failed = 0;
	PMString lastName, firstFailed;
	for (size_t f = 0; f < files.size(); ++f)
	{
		const PMString name(NameOf(files[f]));
		if (OpenDocOfFile(files[f]) != UIDRef::gNull)
		{
			++already;
			continue;
		}
		// WITHOUT A WINDOW AND WITHOUT ITS ALERTS (KFC's own way to open a chapter - memory open-document-hidden): a document with no window has nowhere to
		// show them. Its error, if it would not open, said here and cleared.
		UIDRef docRef;
		const ErrorCode err = Utils<IDocumentCommands>()->Open(&docRef, files[f], kSuppressUI, IOpenFileCmdData::kOpenDefault, IOpenFileCmdData::kUseLockFile,
			kFalse /*showInWindow*/);
		if (err != kSuccess || docRef.GetDataBase() == nil)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			if (failed == 0)
				firstFailed = name;
			++failed;
			continue;
		}
		++opened;
		lastName = name;
	}
	KFC_DIAG_LOG("DOCOPEN files=%d opened=%d already=%d failed=%d", static_cast<int>(files.size()), static_cast<int>(opened), static_cast<int>(already),
		static_cast<int>(failed));
	// "Opened a.indd without a window - Find lists it." / "Opened 2 document(s) without a window - Find lists them."
	if (opened == 1)
	{
		outSaid.Append("Opened ");
		outSaid.Append(lastName);
		outSaid.Append(" without a window - Find lists it.");
	}
	else if (opened > 1)
	{
		outSaid.Append("Opened ");
		AppendDocs(outSaid, opened);
		outSaid.Append(" without a window - Find lists them.");
	}
	if (already > 0)
	{
		if (!outSaid.IsEmpty())
			outSaid.Append(" ");
		AppendDocs(outSaid, already);
		outSaid.Append(" open already - not opened again.");
	}
	if (failed > 0)
	{
		if (!outSaid.IsEmpty())
			outSaid.Append(" ");
		outSaid.Append("Could not open ");
		outSaid.Append(firstFailed);
		if (failed > 1)
		{
			outSaid.Append(" and ");
			outSaid.AppendNumber(failed - 1);
			outSaid.Append(" more");
		}
		outSaid.Append(".");
	}
	outSaid.SetTranslatable(kFalse);
	return opened > 0;
}

// End, KFCDocumentOps.cpp.
