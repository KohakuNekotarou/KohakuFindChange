//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The QR Code tab, listed - see KFCQRSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope, kAllDocumentScope

// General includes:
#include "TextChar.h"					// kTextChar_Ellipse

#include <string>

// Project includes:
#include "KFCQRSearch.h"
#include "IKFCUIServices.h"		// IsQRCodeTabCurrent - the tab is the UI half's (KFCQueryUIServices)
#include "KFCModelTypes.h"		// kKFCQRReach* - what keeps a QR code Find Next found from being selected
#include "KFCDiag.h"
#include "KFCObjectSearch.h"		// ReadItemPlace - a row's place, as an object row's
#include "KFCProgressBar.h"
#include "KFCTextCut.h"			// KFCSafeCut

namespace
{

// A document's slice of the bar (the bar moves between documents: a document's QR codes are read in one go).
const int32 kQRDocSpan = 100;

// A row's content is kept to this many characters - the panel cuts it again to its width when it draws.
const int32 kQRMaxContentChars = 200;

// ONE DOCUMENT'S QR CODES AS ROWS, in the walk's order (FileChapter puts them in page order): at most `limit` (outCapped when
// there were more). false = no walker.
bool WalkDoc(const UIDRef& docRef, size_t limit, std::vector<KFCResultModel::Hit>& outHits, bool& outCapped)
{
	outCapped = false;
	std::vector<UID> frames;
	if (!KFCQRCodes::CollectFrames(docRef, frames))
		return false;
	for (size_t i = 0; i < frames.size(); ++i)
	{
		KFCQRCodes::Kind kind = KFCQRCodes::kKindNone;
		PMString content;
		if (!KFCQRCodes::Read(UIDRef(docRef.GetDataBase(), frames[i]), kind, content))
			continue;
		if (outHits.size() >= limit)
		{
			outCapped = true;
			break;
		}
		KFCResultModel::Hit hit;
		KFCQRSearch::BuildRow(docRef, frames[i], kind, content, hit);
		outHits.push_back(hit);
	}
	return true;
}

}	// anonymous namespace

bool KFCQRSearch::IsTabCurrent(int32* outScope)
{
	int32 scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch qr-tab (the plan's Task 3 cases, made before the tab itself): on = the QR Code tab,
	// whatever the dialog shows; its file's first line "all" = Search: All Documents, anything else Document.
	if (KFC_DIAG_FAULT("qr-tab"))
	{
		current = true;
		answered = true;
		std::string text;
		if (KFCDiagFaultText("qr-tab", text) && text == "all")
			scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (the design's D1 - KFCQRTab in the UI half keeps it from the face's own calls): asked
	// every time. No UI half - InDesign Server, a background thread's session, another build's half - is no QR Code tab.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
		{
			int32 uiScope = IWalkerScopeFactoryUtils::kDocumentScope;
			current = ui->IsQRCodeTabCurrent(uiScope);
			if (uiScope == IWalkerScopeFactoryUtils::kAllDocumentScope)
				scope = uiScope;
		}
	}
	if (outScope != nil)
		*outScope = scope;
	return current;
}

const char* KFCQRSearch::NotChangedWords()
{
	return "these rows are QR codes - nothing is changed from this list (click a row to go to its QR code).";
}

void KFCQRSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved)
{
	(void)allDocuments;		// the targets already say it (KFCSearchEngine's targets for All Documents)
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	// A document's slice dealt out by its size, as every walk's bar is (KFCBookScope::ProgressBases - the progress-share spec).
	const int32 barEnd = static_cast<int32>(targets.size()) * kQRDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;	// the object lists' limit; the kept rows keep theirs
	const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
	for (size_t i = 0; i < targets.size(); ++i)
	{
		KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
		bar.SetPosition(shares[i]);
		if (bar.WasCancelled(kFalse))
		{
			out.cancelled = true;
			break;
		}
		if (static_cast<size_t>(out.total) >= limit)
		{
			out.truncated = true;
			break;
		}
		if (fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		std::vector<KFCResultModel::Hit> hits;
		bool capped = false;
		const bool walked = WalkDoc(docRef, limit - static_cast<size_t>(out.total), hits, capped);
		// A book's chapter goes back now, closed on the spot (one chapter of ours at a time - the other walks' rule).
		if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
			out.unclosed.push_back(targets[i].shortName);
		if (!walked)
		{
			PMString name(targets[i].shortName);
			name.SetTranslatable(kFalse);
			name.Append(": no object walker");
			out.unsearchable.push_back(name);
			continue;
		}
		if (capped)
			out.truncated = true;
		if (!hits.empty())
			KFCSearchEngine::FileChapter(targets[i], hits, out);		// every walk's filing: page order, the tally
		if (capped)
			break;
	}
	if (!out.cancelled && bar.WasCancelled(kFalse))
		out.cancelled = true;
	bar.SetPosition(barEnd);
}

bool KFCQRSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kQRDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, limit, outHits, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kQRDocSpan);
	return walked && !outCancelled;
}

void KFCQRSearch::BuildRow(const UIDRef& docRef, UID frame, KFCQRCodes::Kind kind, const PMString& content,
	KFCResultModel::Hit& out)
{
	out.itemUID = frame;
	// THE ROW'S TEXT (Q7 / Q14): "ID<uid>:<kind>  <content>" - the object rows' "ID<uid>:" head, then the kind and what the
	// QR code holds. Cut where a row's text is kept short, never inside a character (KFCSafeCut); the panel's drawing
	// cuts it again to the panel's width.
	PMString text(content);
	text.SetTranslatable(kFalse);
	if (text.CharCount() > kQRMaxContentChars)
	{
		text.Remove(KFCSafeCut(text, kQRMaxContentChars), kMaxInt32);
		text.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	out.matchText = "ID";
	out.matchText.AppendNumber(static_cast<int32>(frame.Get()));
	out.matchText.Append(":");
	out.matchText.Append(KFCQRCodes::KindWord(kind));
	out.matchText.Append("  ");
	out.matchText.Append(text);
	out.matchText.SetTranslatable(kFalse);
	// WHERE IT STANDS - its page and its words, read as an object row's.
	(void)KFCObjectSearch::ReadItemPlace(docRef, frame, out);
}

bool KFCQRSearch::RowHoldsQRCode(int32 chapterIdx, int32 hitIdx)
{
	const UID frame = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	if (frame == kInvalidUID)
		return false;
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCBookScope::IsDocStillOpen(docRef))
		return true;		// not open now: cannot tell - the row's click opens it and asks again
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || !db->IsValidUID(frame))
		return false;
	KFCQRCodes::Kind kind = KFCQRCodes::kKindNone;
	PMString content;
	return KFCQRCodes::Read(UIDRef(db, frame), kind, content);
}

bool KFCQRSearch::Next(const UIDRef& last, UIDRef& outFrame, int32& outReach, PMString& outWords)
{
	outFrame = UIDRef();
	outReach = 0;
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	// THE RANGE - the tab's Search: as the list takes it (ResolveRunScope's two, without a book: Find Next is the dialog's).
	int32 scope = IWalkerScopeFactoryUtils::kDocumentScope;
	(void)IsTabCurrent(&scope);
	const bool allDocuments = (scope == IWalkerScopeFactoryUtils::kAllDocumentScope);
	std::vector<KFCBookScope::ChapterDoc> docs;
	if (allDocuments)
		KFCBookScope::ListOpenDocuments(docs);
	else
	{
		IDocument* const doc = KFCBookScope::ActiveDocument();		// non-owning
		if (doc != nil)
			docs.push_back(KFCBookScope::DocAsChapter(doc));
	}
	if (docs.empty())
	{
		outWords.Append("No document is open.");
		return false;
	}
	// EVERY QR CODE OF THE RANGE IN THE LIST'S ORDER (Q10): document by document, each one's in page order - the order the
	// list files them in (KFCSearchEngine::FileChapter). Walked anew at every press: what was added or taken away is met.
	struct Found
	{
		size_t					doc;
		KFCResultModel::Hit		hit;
	};
	std::vector<Found> found;
	for (size_t d = 0; d < docs.size(); ++d)
	{
		std::vector<KFCResultModel::Hit> hits;
		bool capped = false;
		if (!WalkDoc(docs[d].docRef, static_cast<size_t>(KFCResultModel::kKFCCollectObjectLimit), hits, capped))
			continue;
		KFCResultModel::OrderHitsByPage(hits);
		for (size_t h = 0; h < hits.size(); ++h)
		{
			Found one;
			one.doc = d;
			one.hit = hits[h];
			found.push_back(one);
		}
	}
	if (found.empty())
	{
		if (allDocuments)
		{
			outWords.Append("No QR codes in ");
			outWords.AppendNumber(static_cast<int32>(docs.size()));
			outWords.Append(" open document(s).");
		}
		else
		{
			outWords.Append("No QR codes in document \"");
			outWords.Append(docs[0].shortName);
			outWords.Append("\".");
		}
		return false;
	}
	// THE ONE AFTER `last` - the first when `last` is not among them - and after the last one the first again, silently.
	size_t next = 0;
	for (size_t k = 0; k < found.size(); ++k)
	{
		if (last.GetDataBase() != nil && docs[found[k].doc].docRef.GetDataBase() == last.GetDataBase()
			&& found[k].hit.itemUID == last.GetUID())
		{
			next = (k + 1) % found.size();
			break;
		}
	}
	const Found& one = found[next];
	outFrame = UIDRef(docs[one.doc].docRef.GetDataBase(), one.hit.itemUID);
	if (one.hit.isLocked)
		outReach |= kKFCQRReachLocked;
	if (one.hit.isHidden)
		outReach |= kKFCQRReachHidden;
	if (one.hit.onHiddenSpread)
		outReach |= kKFCQRReachHiddenSpread;
	if (one.hit.isOverset)
		outReach |= kKFCQRReachOverset;
	// THE LINE: the row's text without its "ID<uid>:" head - "<kind>  <content>" - then where it stands, and its document
	// over All Documents.
	PMString text(one.hit.matchText);
	text.SetTranslatable(kFalse);
	const CharCounter colon = text.IndexOfWChar(UTF32TextChar(':'), 0);
	if (colon >= 0)
		text.Remove(0, colon + 1);
	outWords.Append(text);
	// (Overset first: an item no spread holds has no owner page either, so it reads "on the pasteboard" as well - ReadItemPlace.)
	if (one.hit.isOverset)
		outWords.Append(" - overset text");
	else if (one.hit.onPasteboard)
		outWords.Append(" - pasteboard");
	else
	{
		outWords.Append(" - page ");
		outWords.Append(one.hit.pageString);
	}
	if (allDocuments)
	{
		outWords.Append(" - ");
		outWords.Append(docs[one.doc].shortName);
	}
	return true;
}

// End, KFCQRSearch.cpp.
