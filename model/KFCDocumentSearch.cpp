//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Document kind, listed - see KFCDocumentSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocument.h"

// General includes:
#include "FileUtils.h"				// IDFileToPMString - the row's path
#include "IDFile.h"

#include <string>

// Project includes:
#include "KFCDocumentSearch.h"
#include "IKFCUIServices.h"			// the tab and the windows are the UI half's
#include "KFCDiag.h"

namespace
{

// "1 window" / "2 windows" - a count and its noun, as a person reads them.
void AppendCount(PMString& io, int32 n, const char* one, const char* many)
{
	io.AppendNumber(n);
	io.Append(" ");
	io.Append(n == 1 ? one : many);
}

}	// anonymous namespace

bool KFCDocumentSearch::IsTabCurrent()
{
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch document-tab (the model's cases, which need no dialog): on = the Document kind.
	if (KFC_DIAG_FAULT("document-tab"))
		return true;
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCStorySearch::IsTabCurrent's rule): no UI half is no Document kind.
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	return ui != nil && ui->DocumentTabCurrent();
}

bool KFCDocumentSearch::RowOf(const UIDRef& docRef, const PMString& name, KFCResultModel::Hit& outHit)
{
	outHit = KFCResultModel::Hit();
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
	if (db == nil || doc == nil)
		return false;
	// ITS FACTS (W3): its windows - the layout ones, then the Story Editor's -, whether it was ever saved and whether it holds changes, and where its file is.
	PMString facts;
	facts.SetTranslatable(kFalse);
	int32 layout = 0, other = 0;
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	if (ui != nil)
		ui->CountDocWindows(docRef, layout, other);
	if (layout == 0)
		facts.Append((other > 0) ? "no layout window" : "no window");		// (a Story Editor's window alone: there IS a window - the re-inspection of 11-06)
	else
		AppendCount(facts, layout, "window", "windows");
	if (other > 0)
	{
		facts.Append(", ");
		facts.AppendNumber(other);
		facts.Append(" Story Editor");
	}
	const IDFile* const file = db->GetSysFile();
	if (file == nil)
		facts.Append(", not saved");
	else if (db->IsModified())
		facts.Append(", modified");		// (saved and unchanged: nothing said)
	PMString path;
	if (file != nil)
		FileUtils::IDFileToPMString(*file, path);
	path.SetTranslatable(kFalse);

	// THE ROW: the document - no story, no item (the text doors pass a row with neither by - KFCResultModel::Hit's note); its name, then its facts as a Story
	// row's are drawn (runChain), then the file's path. No page: it sorts and draws as the list's own (KFCResultModel::BuildHitLocator says nothing for it).
	outHit.holderKind = kKFCHolderDocument;
	outHit.preText = name;
	outHit.preText.Append("  ");
	outHit.preText.SetTranslatable(kFalse);
	outHit.runChain = facts;
	if (!path.IsEmpty())
	{
		outHit.postText = PMString(" - ");
		outHit.postText.Append(path);
	}
	outHit.postText.SetTranslatable(kFalse);
	outHit.isOverset = false;
	KFC_DIAG_LOG("DOCROW %s layout=%d other=%d saved=%d modified=%d", name.GetUTF8String().c_str(), static_cast<int>(layout), static_cast<int>(other),
		(file != nil) ? 1 : 0, db->IsModified() ? 1 : 0);
	return true;
}

void KFCDocumentSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, KFCSearchEngine::CollectTally& out)
{
	out = KFCSearchEngine::CollectTally();
	for (size_t i = 0; i < targets.size(); ++i)
	{
		// A BOOK'S CHAPTER: open now or not, asked by its file - ListBookChapters opens nothing and leaves every chapter's docRef null (the run opens each in
		// turn); FindOpenChapterDoc opens nothing either (the re-inspection of 11-06: read as "null = closed", every chapter of a book was passed by).
		if (fromBook && targets[i].docRef == UIDRef::gNull)
		{
			UIDRef open;
			if (KFCBookScope::FindOpenChapterDoc(targets[i].file, open))
				targets[i].docRef = open;
		}
		// A BOOK'S CHAPTER THAT IS CLOSED IS NOT LISTED (W3 - what is looked for is a document open and not seen): not opened, not said.
		if (targets[i].docRef == UIDRef::gNull)
			continue;
		std::vector<KFCResultModel::Hit> hits(1);
		if (!RowOf(targets[i].docRef, targets[i].shortName, hits[0]))
			continue;
		KFCSearchEngine::FileChapter(targets[i], hits, out);
	}
}

// End, KFCDocumentSearch.cpp.
