//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCDiagWrittenDocs.h. Test builds only: without KFC_DIAG this file compiles to nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#ifdef KFC_DIAG

// Interface includes:
#include "ICmdHistory.h"			// GetUndoStepCount / GetRedoStepCount - was the write undone since
#include "ICommandProcessor.h"		// only to reach the boss ICmdHistory lives on (ISession::QueryCommandProcessor)
#include "IDataBase.h"
#include "IDocument.h"
#include "ISession.h"

// General includes:
#include "PMString.h"
#include "SDKFileHelper.h"

#include <chrono>
#include <string>
#include <vector>

// Project includes:
#include "KFCBookScope.h"			// IsDocStillOpen - a document asked about without following its address
#include "KFCDiag.h"				// KFC_DIAG_LOG
#include "KFCDiagWrittenDocs.h"

namespace
{
	// A document KFC wrote, not saved since and still open: taken off at its close (Closing), or once it has been saved
	// (Check).
	struct Written
	{
		UIDRef				doc;		// compared, never followed once its document has closed - it goes at the close
		std::string			name;		// the document's name at the write (UTF-8)
		std::string			path;		// its file then ("" = none: an untitled document, or a conversion)
		unsigned long long	fileTime;	// that file's last write time then (0 = none)
		int32				undoSteps;	// the document's undo steps just after the write - the write the newest (-1 = unread)
		int32				redoSteps;	// ...and its redo steps then (-1 = unread)
		Written() : fileTime(0), undoSteps(-1), redoSteps(-1) {}
	};
	std::vector<Written> gWritten;

	// A close KFC asked InDesign for, and when.
	struct Asked
	{
		UIDRef	doc;
		double	atMs;
		Asked() : atMs(0.0) {}
	};
	std::vector<Asked> gAsked;

	// HOW LONG A CLOSE KFC ASKED FOR IS TAKEN TO BE THE CLOSE THAT COMES. One asked for now (kProcess) arrives inside the
	// call; one on a schedule before the next input (measured: 77 ms - docs/ai-notes/kbs-block12-defect-recheck-2026-10-03.md,
	// L-2), or as a run's progress bar comes down. One InDesign never carries out - asked for while a background export
	// holds the document, the request is dropped (docs/ai-notes/kbs-scenario4-defect-recheck-2026-10-04.md) - must not make
	// a close somebody makes later KFC's.
	const double kAskedWindowMs = 3000.0;

	// The checks standing now: one inside another (a Replace of several rows that is one row's) is checked with the outer.
	int32 gDepth = 0;

	double NowMs()
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// The document's name and file, and that file's last write time - read now, the document open.
	void ReadDoc(const UIDRef& docRef, std::string& outName, std::string& outPath, unsigned long long& outFileTime)
	{
		outName.clear();
		outPath.clear();
		outFileTime = 0;
		IDataBase* const db = docRef.GetDataBase();
		if (db == nil)
			return;
		InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
		if (doc != nil)
		{
			PMString name;
			doc->GetName(name);
			outName = name.GetUTF8String();
		}
		const IDFile* const file = db->GetSysFile();
		if (file == nil)
			return;
		outPath = SDKFileHelper(*file).GetPath().GetUTF8String();
		if (outPath.empty())
			return;
		const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, outPath.c_str(), -1, nil, 0);
		if (wideLength <= 0)
			return;
		std::wstring widePath(static_cast<size_t>(wideLength), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, 0, outPath.c_str(), -1, &widePath[0], wideLength) <= 0)
			return;
		WIN32_FILE_ATTRIBUTE_DATA data;
		if (::GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &data))
			outFileTime = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32)
				| data.ftLastWriteTime.dwLowDateTime;
	}

	// Saved since the entry was made: another file now (Save As), or the file written after it.
	bool SavedSince(const Written& w)
	{
		std::string name, path;
		unsigned long long fileTime = 0;
		ReadDoc(w.doc, name, path, fileTime);
		return path != w.path || fileTime != w.fileTime;
	}

	// Would a close without saving throw something away - IDataBase::IsModified, "modified since last save"
	// (IDataBase.h). KFC closes only documents with a file or a conversion it marked clean as it opened it
	// (KFCBookScope's HasUnsavedWork), for which this is the question.
	bool IsUnsaved(const UIDRef& docRef)
	{
		IDataBase* const db = docRef.GetDataBase();
		return db != nil && db->IsModified() != kFalse;
	}

	// How many undo (or redo) steps the document has now - ICmdHistory on the session's command processor, the route
	// KIDMCP's KIDMCPPencilUndo.cpp reads it by (a document's target is its root). -1 when it cannot be read.
	int32 HistorySteps(const UIDRef& docRef, bool redo)
	{
		IDataBase* const db = docRef.GetDataBase();
		ISession* const session = GetExecutionContextSession();
		if (db == nil || session == nil)
			return -1;
		InterfacePtr<ICommandProcessor> processor(session->QueryCommandProcessor());
		InterfacePtr<ICmdHistory> history(processor, UseDefaultIID());
		if (history == nil)
			return -1;
		const UIDRef target(db, db->GetRootUID());
		return redo ? history->GetRedoStepCount(target) : history->GetUndoStepCount(target);
	}

	// THE WRITE TAKEN BACK BY AN UNDO: fewer undo steps now than just after the write, and more redo steps. Measured
	// 2026-10-12 (case qs-book-undo): a query run's Ctrl+Z puts the chapters back AND InDesign takes their "unsaved" flag
	// down - they are as their files are - so a close then loses nothing, and is not the flag gone down over a write that
	// CLEANED means. (A flag taken down over a write that stands - the fault switches clean-after-write and
	// landing-guard-off - leaves the steps where they were, and is still CLEANED.) Fewer undo steps ALONE is not an undo:
	// InDesign's memory purge drops old undo steps too (1525 down to 6 - docs/ai-notes/kidmcp-verify-v2-live-2026-10-12.md)
	// and makes nothing redoable - with the flag down over a write that stands, that is the CLEANED this check is for.
	bool Undone(const Written& w)
	{
		const int32 undoNow = HistorySteps(w.doc, false);
		const int32 redoNow = HistorySteps(w.doc, true);
		return w.undoSteps >= 0 && w.redoSteps >= 0 && undoNow >= 0 && redoNow >= 0
			&& undoNow < w.undoSteps && redoNow > w.redoSteps;
	}

	int32 WrittenIndex(const UIDRef& docRef)
	{
		for (size_t i = 0; i < gWritten.size(); ++i)
		{
			if (gWritten[i].doc == docRef)
				return static_cast<int32>(i);
		}
		return -1;
	}
}

void KFCDiagWrittenDocs::NoteWrite(const UIDRef& docRef)
{
	if (docRef.GetDataBase() == nil)
		return;
	Written w;
	w.doc = docRef;
	ReadDoc(docRef, w.name, w.path, w.fileTime);
	w.undoSteps = HistorySteps(docRef, false);	// every caller notes a write after its step has ended (KFCDiagWrittenDocs.h)
	w.redoSteps = HistorySteps(docRef, true);
	// The file as it is at this write: written before it, it was saved before every write on the list.
	const int32 at = WrittenIndex(docRef);
	if (at >= 0)
		gWritten[static_cast<size_t>(at)] = w;
	else
		gWritten.push_back(w);
	KFC_DIAG_LOG("WRITTENDOCS wrote %s unsaved=%d file=%s", w.name.c_str(), IsUnsaved(docRef) ? 1 : 0, w.path.c_str());
}

void KFCDiagWrittenDocs::NoteCloseAsked(const UIDRef& docRef)
{
	const double now = NowMs();
	for (size_t i = gAsked.size(); i-- > 0;)
	{
		if (now - gAsked[i].atMs > kAskedWindowMs || gAsked[i].doc == docRef)
			gAsked.erase(gAsked.begin() + i);
	}
	Asked asked;
	asked.doc = docRef;
	asked.atMs = now;
	gAsked.push_back(asked);
	std::string name, path;
	unsigned long long fileTime = 0;
	ReadDoc(docRef, name, path, fileTime);
	KFC_DIAG_LOG("WRITTENDOCS close-asked %s unsaved=%d written=%d", name.c_str(), IsUnsaved(docRef) ? 1 : 0,
		WrittenIndex(docRef) >= 0 ? 1 : 0);
}

void KFCDiagWrittenDocs::Closing(const UIDRef& docRef)
{
	const double now = NowMs();
	bool askedByKFC = false;
	double askedMsAgo = -1.0;
	for (size_t i = gAsked.size(); i-- > 0;)
	{
		if (!(gAsked[i].doc == docRef))
			continue;
		if (now - gAsked[i].atMs <= kAskedWindowMs)
		{
			askedByKFC = true;
			askedMsAgo = now - gAsked[i].atMs;
		}
		gAsked.erase(gAsked.begin() + i);
	}
	const int32 at = WrittenIndex(docRef);
	if (!askedByKFC && at < 0)
		return;		// neither a document KFC wrote nor a close it asked for
	std::string name, path;
	unsigned long long fileTime = 0;
	ReadDoc(docRef, name, path, fileTime);
	const bool unsaved = IsUnsaved(docRef);
	const bool written = (at >= 0);
	const bool savedSince = written && SavedSince(gWritten[static_cast<size_t>(at)]);
	const bool undone = written && Undone(gWritten[static_cast<size_t>(at)]);
	if (askedByKFC && unsaved)
	{
		// KFC asked only once nothing in it was unsaved - something was written between the asking and the close, and the
		// close, without UI, throws it away.
		KFC_DIAG_LOG("WRITTENDOCS LOST %s by=kfc written=%d askedMsAgo=%.0f file=%s", name.c_str(), written ? 1 : 0,
			askedMsAgo, path.c_str());
	}
	else if (written && !savedSince && !unsaved && !undone)
	{
		// Written by KFC, the file not written since, the write not undone, and nothing unsaved: the flag went down over the
		// write.
		KFC_DIAG_LOG("WRITTENDOCS CLEANED %s at=close by=%s file=%s", name.c_str(), askedByKFC ? "kfc" : "other", path.c_str());
	}
	else
	{
		KFC_DIAG_LOG("WRITTENDOCS closed %s by=%s unsaved=%d written=%d saved=%d undone=%d", name.c_str(),
			askedByKFC ? "kfc" : "other", unsaved ? 1 : 0, written ? 1 : 0, savedSince ? 1 : 0, undone ? 1 : 0);
	}
	if (written)
		gWritten.erase(gWritten.begin() + static_cast<size_t>(at));
}

KFCDiagWrittenDocs::Check::Check(const char* where)
	: fWhere(where)
{
	++gDepth;
}

KFCDiagWrittenDocs::Check::~Check()
{
	if (--gDepth > 0)
		return;
	std::string listed;
	int32 docs = 0;
	for (size_t i = gWritten.size(); i-- > 0;)
	{
		const Written w = gWritten[i];
		// Asked without following the address: every close is heard (Closing), so one missing here was closed unheard.
		if (!KFCBookScope::IsDocStillOpen(w.doc))
		{
			KFC_DIAG_LOG("WRITTENDOCS gone %s at=%s file=%s - its close was not heard", w.name.c_str(), fWhere, w.path.c_str());
			gWritten.erase(gWritten.begin() + i);
			continue;
		}
		if (SavedSince(w))
		{
			gWritten.erase(gWritten.begin() + i);
			continue;
		}
		const bool unsaved = IsUnsaved(w.doc);
		const bool undone = !unsaved && Undone(w);
		if (!unsaved && !undone)
			KFC_DIAG_LOG("WRITTENDOCS CLEANED %s at=%s file=%s", w.name.c_str(), fWhere, w.path.c_str());
		++docs;
		listed = " [" + w.name + (unsaved ? "|unsaved]" : (undone ? "|undone]" : "|CLEAN]")) + listed;
	}
	KFC_DIAG_LOG("WRITTENDOCS %s docs=%d%s", fWhere, static_cast<int>(docs), listed.c_str());
}

#endif // KFC_DIAG

// End, KFCDiagWrittenDocs.cpp.
