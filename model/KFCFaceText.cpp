//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ face's buttons for Ruby and Kenten - see KFCFaceText.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "ITextModel.h"
#include "IWalkerScopeFactoryUtils.h"	// the face's Search: values

#include <memory>
#include <vector>

// Project includes:
#include "KFCFaceText.h"
#include "KFCBookScope.h"				// the documents the buttons look in
#include "KFCDiag.h"
#include "KFCKentenSearch.h"
#include "KFCReplaceEngine.h"			// WriteFacePlaces - a row's Replace's writers
#include "KFCResultModel.h"				// Hit, OrderHitsByPage, MarkUpBreaksForDisplay
#include "KFCRubyList.h"				// Readings - Inconsistent Ruby's count
#include "KFCRubyListFile.h"			// a ruby list's words, read for every press
#include "KFCRubySearch.h"
#include "KFCSearchEngine.h"			// the part of Search:, ReadText, the frame's lock
#include "KFCTextCut.h"					// KFCSafeCut

namespace
{
// What a row's words hold of its text when it draws no ruby of its own, at most (a QR code row's line's length).
const int32 kFaceWordsMost = 40;

// WHAT THE BUTTONS ASK NOW: the kind's conditions - the face's - with what its search reads first: a ruby list's words. False = refused, outWhy says why (the panel's Find's doors, in the face's words).
struct Asked
{
	int32					mode;
	KFCRubyConditions		ruby;
	KFCKentenConditions		kenten;

	Asked() : mode(kKFCRubySearchMode) {}
	int32 Scope() const { return (mode == kKFCKentenSearchMode) ? kenten.scope : ruby.scope; }
};

bool ReadAsked(int32 mode, Asked& out, PMString& outWhy)
{
	out.mode = mode;
	if (mode == kKFCKentenSearchMode)
	{
		(void)KFCKentenSearch::IsTabCurrent(&out.kenten);
		if (out.kenten.kind == kKFCKentenFindWithout && out.kenten.base.IsEmpty())
		{
			outWhy.Append("Text Without Kenten looks for a text: type it in Base Text.");
			return false;
		}
		return true;
	}
	(void)KFCRubySearch::IsTabCurrent(&out.ruby);
	if (out.ruby.kind == kKFCRubyFindWithout && out.ruby.base.IsEmpty())
	{
		outWhy.Append("Text Without Ruby looks for a text: type it in Base Text.");
		return false;
	}
	if (out.ruby.kind == kKFCRubyFindList)
	{
		int32 words = 0;
		if (!KFCRubyListFile::Load(out.ruby.listPath, out.ruby.listWords, words, outWhy))
			return false;
	}
	return true;
}

// WHERE THE BUTTONS LOOK (KFCTableSearch's ButtonRange): the Search: it was read for, the front document it was read in, whether the selection did not offer it (then the whole document, and said) - and the part. KEPT WHILE THE
// SELECTION IS AS FIND NEXT LEFT IT: the selection is what was found then, not what Search: was read from.
struct Range
{
	int32										scope;		// -1 = not read
	const IDataBase*							db;
	bool										fellBack;
	std::vector<KFCSearchEngine::ListedPlace>	part;

	Range() : scope(-1), db(nil), fellBack(false) {}
};
Range gRange;

bool ScopeIsPartOfDocument(int32 scope)
{
	return scope == IWalkerScopeFactoryUtils::kStoryScope || scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope || scope == IWalkerScopeFactoryUtils::kSelectionScope;
}

// The Text tab's sentence for a Search: the selection does not offer (KFCTableSearch's).
void AppendFellBack(PMString& io, int32 scope)
{
	io.Append("Search: is ");
	io.Append(scope == IWalkerScopeFactoryUtils::kStoryScope ? "Story" : scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope ? "To End of Story" : "Selection");
	io.Append(scope == IWalkerScopeFactoryUtils::kSelectionScope ? ", but no text is selected"
		: scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope ? ", but there is no text cursor"
		: ", but no text or text frame is selected");
	io.Append(" - the whole document was searched, as Edit > Find/Change does.");
}

struct Row
{
	size_t					doc;
	KFCResultModel::Hit		hit;
};

// EVERY PLACE OF THE RANGE IN THE LIST'S ORDER: document by document, each in page order. Inconsistent Ruby's readings counted over every document of the range first (the spec's P3-4). False = it did not look: outRefusal says why.
bool WalkRange(const KFCTableLook& look, Asked& asked, std::vector<KFCBookScope::ChapterDoc>& outDocs, std::vector<Row>& outRows, PMString& outRefusal)
{
	const int32 scope = asked.Scope();
	if (scope == IWalkerScopeFactoryUtils::kAllDocumentScope)
		KFCBookScope::ListOpenDocuments(outDocs);
	else
	{
		IDocument* const doc = KFCBookScope::ActiveDocument();		// non-owning
		if (doc != nil)
			outDocs.push_back(KFCBookScope::DocAsChapter(doc));
	}
	if (outDocs.empty())
	{
		outRefusal.Append("No document is open.");
		return false;
	}
	IDataBase* const front = outDocs[0].docRef.GetDataBase();
	if (!look.onLast || gRange.scope != scope || (ScopeIsPartOfDocument(scope) && gRange.db != front))
	{
		gRange = Range();
		gRange.db = front;
		if (ScopeIsPartOfDocument(scope))
		{
			if (KFCSearchEngine::SearchScopeForSelection(scope) != scope)
				gRange.fellBack = true;
			else if (!KFCSearchEngine::PartStretches(scope, front, gRange.part))
			{
				outRefusal.Append("What Search: holds in the selection could not be read - nothing was searched.");
				return false;
			}
		}
		gRange.scope = scope;
	}
	const std::vector<KFCSearchEngine::ListedPlace>* const part = (ScopeIsPartOfDocument(scope) && !gRange.fellBack) ? &gRange.part : nil;
	if (asked.mode == kKFCRubySearchMode && asked.ruby.kind == kKFCRubyFindVaries)
	{
		std::shared_ptr<KFCRubyList::Readings> counted(new KFCRubyList::Readings);
		for (size_t d = 0; d < outDocs.size(); ++d)
			KFCRubySearch::CountReadings(outDocs[d].docRef, asked.ruby.switches, part, *counted);
		asked.ruby.varies = counted;
	}
	for (size_t d = 0; d < outDocs.size(); ++d)
	{
		std::vector<KFCResultModel::Hit> hits;
		const bool walked = (asked.mode == kKFCKentenSearchMode) ? KFCKentenSearch::WalkForFace(outDocs[d].docRef, asked.kenten, part, hits)
			: KFCRubySearch::WalkForFace(outDocs[d].docRef, asked.ruby, part, hits);
		if (!walked)
			continue;
		KFCResultModel::OrderHitsByPage(hits);
		for (size_t h = 0; h < hits.size(); ++h)
		{
			Row one;
			one.doc = d;
			one.hit = hits[h];
			outRows.push_back(one);
		}
	}
	return true;
}

// The row after the place found last - the first when that one is not among them.
size_t RowAfterLast(const KFCTableLook& look, const std::vector<KFCBookScope::ChapterDoc>& docs, const std::vector<Row>& rows)
{
	if (look.lastDoc == UIDRef::gNull)
		return 0;
	for (size_t k = 0; k < rows.size(); ++k)
		if (docs[rows[k].doc].docRef == look.lastDoc && rows[k].hit.storyUID == look.lastStory.GetUID() && rows[k].hit.textStart == look.lastAt)
			return (k + 1) % rows.size();
	return 0;
}

// FIND NEXT'S ROW: from the selection - the first place of its story that starts at its end or after it, as the Text tab's Find Next goes on after the selection (measured - docs/ai-notes/kfc-native-text-tab-measure-2026-11-01.md);
// past the story's last, the row after it. While the selection is as the last landing left it, the row after that one.
size_t PickNextRow(const KFCTableLook& look, const std::vector<KFCBookScope::ChapterDoc>& docs, const std::vector<Row>& rows)
{
	if (look.onLast || look.selStory == UIDRef::gNull)
		return RowAfterLast(look, docs, rows);
	bool inStory = false;
	size_t lastOfStory = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (docs[rows[k].doc].docRef.GetDataBase() != look.selStory.GetDataBase() || rows[k].hit.storyUID != look.selStory.GetUID())
			continue;
		if (rows[k].hit.textStart >= look.selEnd)
			return k;
		inStory = true;
		lastOfStory = k;
	}
	return inStory ? (lastOfStory + 1) % rows.size() : RowAfterLast(look, docs, rows);
}

// What a row says on the face's first line: what it draws - its ruby, its marks - or its text, cut.
PMString RowSays(const UIDRef& docRef, const KFCResultModel::Hit& hit)
{
	PMString words(hit.runChain);
	words.SetTranslatable(kFalse);
	if (!words.IsEmpty())
		return words;
	words = KFCSearchEngine::ReadText(UIDRef(docRef.GetDataBase(), hit.storyUID), hit.textStart, hit.textEnd - hit.textStart);
	words.SetTranslatable(kFalse);
	KFCResultModel::MarkUpBreaksForDisplay(words);
	if (words.CharCount() > kFaceWordsMost)
	{
		const int32 keep = KFCSafeCut(words, kFaceWordsMost);
		words.Remove(keep > 0 ? keep : kFaceWordsMost, kMaxInt32);
		words.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	return words;
}

// What a place reads as now, after a write: its ruby, or its kenten - or, for a mark made ruby (its text moved), plainly that.
PMString WrittenWords(const Asked& asked, const UIDRef& story, TextIndex start, TextIndex end)
{
	PMString words;
	words.SetTranslatable(kFalse);
	if (asked.mode == kKFCKentenSearchMode)
		words = KFCKentenSearch::RowWords(story, start, end, KFCKentenSearch::MarkOn(story, start, end));
	else if (asked.ruby.kind == kKFCRubyFindMarks)
		words.Append("the mark is ruby now");
	else
		words = KFCRubySearch::RowWords(story, start, end);
	if (words.IsEmpty())
		words = KFCSearchEngine::ReadText(story, start, end - start);		// (no ruby or mark on it now: its text)
	words.SetTranslatable(kFalse);
	return words;
}
}	// anonymous namespace

bool KFCFaceText::Next(int32 mode, const KFCTableLook& look, KFCTableFound& out)
{
	out = KFCTableFound();
	out.words.SetTranslatable(kFalse);
	out.note.SetTranslatable(kFalse);
	Asked asked;
	if (!ReadAsked(mode, asked, out.words))
		return false;
	std::vector<KFCBookScope::ChapterDoc> docs;
	std::vector<Row> rows;
	if (!WalkRange(look, asked, docs, rows, out.words) || rows.empty())
		return false;
	const Row& one = rows[PickNextRow(look, docs, rows)];
	const UIDRef docRef(docs[one.doc].docRef);
	out.doc = docRef;
	out.story = UIDRef(docRef.GetDataBase(), one.hit.storyUID);
	out.at = one.hit.textStart;
	out.end = one.hit.textEnd;
	if (one.hit.isLocked)
		out.reach |= kKFCQRReachLocked;
	if (one.hit.isHidden)
		out.reach |= kKFCQRReachHidden;
	if (one.hit.onHiddenSpread)
		out.reach |= kKFCQRReachHiddenSpread;
	if (one.hit.isOverset)
		out.reach |= kKFCQRReachOverset;
	// THE LINE: what its row draws, then where it stands, and its document over All Documents (KFCTableSearch::Next's).
	out.words.Append(RowSays(docRef, one.hit));
	if (one.hit.isOverset)
		out.words.Append(" - overset text");
	else if (one.hit.frameOnPasteboard)
		out.words.Append(" - pasteboard");
	else
	{
		out.words.Append(" - page ");
		out.words.Append(one.hit.pageString);
	}
	if (asked.Scope() == IWalkerScopeFactoryUtils::kAllDocumentScope)
	{
		out.words.Append(" - ");
		out.words.Append(docs[one.doc].shortName);
	}
	if (gRange.fellBack)
		AppendFellBack(out.note, asked.Scope());
	KFC_DIAG_LOG("FACETEXT next mode=%d story=%d at=%d end=%d reach=%d rows=%d words=%s", static_cast<int>(mode), static_cast<int>(out.story.GetUID().Get()), static_cast<int>(out.at), static_cast<int>(out.end),
		static_cast<int>(out.reach), static_cast<int>(rows.size()), out.words.GetUTF8String().c_str());
	return true;
}

bool KFCFaceText::ChangeFound(int32 mode, const KFCTableFound& found, PMString& outWords)
{
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	Asked asked;
	if (!ReadAsked(mode, asked, outWords))
		return false;
	// THE DOORS - nothing written yet. The document first: everything after it follows its database.
	if (found.doc == UIDRef::gNull || !KFCBookScope::IsDocStillOpen(found.doc))
	{
		outWords.Append("That text's document has been closed - press Find Next.");
		return false;
	}
	InterfacePtr<ITextModel> model(found.story, UseDefaultIID());
	if (model == nil || found.end > model->TotalLength())
	{
		outWords.Append("That text is no longer there - press Find Next.");
		return false;
	}
	if (!KFCSearchEngine::IsFrameEditable(found.story, KFCSearchEngine::EditableFrameForMatch(found.story, found.at)))
	{
		outWords.Append("That text is locked - not changed.");
		return false;
	}
	KFCReplaceEngine::FacePlace place;
	place.doc = found.doc;
	place.story = found.story.GetUID();
	place.start = found.at;
	place.end = found.end;
	KFCReplaceEngine::FaceWrite wrote;
	if (!KFCReplaceEngine::WriteFacePlaces(mode, asked.ruby, std::vector<KFCReplaceEngine::FacePlace>(1, place), wrote))
	{
		outWords = wrote.why;
		return false;
	}
	if (wrote.written == 0)
	{
		outWords.Append((wrote.asAsked > 0) ? PMString("That text already reads as the Change side asks.") : wrote.refusal);
		outWords.SetTranslatable(kFalse);
		return false;
	}
	outWords.Append("Changed: ");
	outWords.Append(WrittenWords(asked, found.story, found.at, (asked.ruby.kind == kKFCRubyFindMarks && mode == kKFCRubySearchMode) ? found.at : found.end));
	KFC_DIAG_LOG("FACETEXT change wrote words=%s", outWords.GetUTF8String().c_str());
	return true;
}

bool KFCFaceText::ChangeAll(int32 mode, const KFCTableLook& look, int32& outChanged, int32& outMatched, PMString& outWords)
{
	outChanged = 0;
	outMatched = 0;
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	Asked asked;
	if (!ReadAsked(mode, asked, outWords))
		return false;
	std::vector<KFCBookScope::ChapterDoc> docs;
	std::vector<Row> rows;
	if (!WalkRange(look, asked, docs, rows, outWords))
		return false;
	outMatched = static_cast<int32>(rows.size());
	std::vector<KFCReplaceEngine::FacePlace> places;
	int32 locked = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (rows[k].hit.isLocked)
		{
			++locked;
			continue;
		}
		KFCReplaceEngine::FacePlace place;
		place.doc = docs[rows[k].doc].docRef;
		place.story = rows[k].hit.storyUID;
		place.start = rows[k].hit.textStart;
		place.end = rows[k].hit.textEnd;
		places.push_back(place);
	}
	KFCReplaceEngine::FaceWrite wrote;
	if (!places.empty() && !KFCReplaceEngine::WriteFacePlaces(mode, asked.ruby, places, wrote))
	{
		outWords = wrote.why;
		return false;
	}
	outChanged = wrote.written;
	outWords.AppendNumber(outChanged);
	outWords.Append(" of ");
	outWords.AppendNumber(outMatched);
	outWords.Append(" place(s) changed.");
	if (locked + wrote.asAsked + wrote.refused > 0)
	{
		outWords.Append(" Left as they were:");
		const char* sep = " ";
		if (locked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(locked);
			outWords.Append(" locked");
			sep = ", ";
		}
		if (wrote.asAsked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(wrote.asAsked);
			outWords.Append(" already as asked");
			sep = ", ";
		}
		if (wrote.refused > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(wrote.refused);
			outWords.Append(" not as the Change side reads them");
		}
		outWords.Append(".");
	}
	KFC_DIAG_LOG("FACETEXT changeall changed=%d matched=%d words=%s", static_cast<int>(outChanged), static_cast<int>(outMatched), outWords.GetUTF8String().c_str());
	return true;
}

// End, KFCFaceText.cpp.
