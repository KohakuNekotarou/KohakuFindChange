//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ FACE'S BUTTONS FOR WHAT STANDS IN A STORY'S TEXT - Ruby's and Kenten's (docs/superpowers/specs/2026-11-02-kfc-kohaku-face-buttons-design.md B1 - B5; the author, 11-02: "can Find Next and the rest go on
//  Ruby and Kenten too?"). The Table part's are KFCTableSearch's; these are their shape:
//    Find Next   the next place the kind finds over the face's Search: - from the selection, or on from the place found last while the selection is as its landing left it; after the last, the first again, silently.
//    Change      the kind's Change side on the place Find Next found, one Ctrl+Z.
//    Change All  every place of Search:, one Ctrl+Z, said in InDesign's words.
//  What is found is what a list's row would be (the walks of KFCRubySearch and KFCKentenSearch, no list's limit), and what is written is what that row's Replace writes (KFCReplaceEngine::WriteFacePlaces). Open
//  documents only - the front one, or every one with All Documents: a book is the panel's.
//
//========================================================================================

#ifndef __KFCFaceText_h__
#define __KFCFaceText_h__

#include "KFCModelTypes.h"		// KFCTableLook, KFCTableFound - the Table part's own, which say a place as well

namespace KFCFaceText
{
	/** FIND NEXT. mode = kKFCRubySearchMode or kKFCKentenSearchMode. out.table is kInvalidUID; out.words the face's first line. False = nothing there, or it did not look - out.words says why (empty: nothing found,
	    and InDesign's own alert is the face's to raise). */
	bool Next(int32 mode, const KFCTableLook& look, KFCTableFound& out);

	/** CHANGE on what Find Next found. outWords = what it reads now, or why it was not written. */
	bool ChangeFound(int32 mode, const KFCTableFound& found, PMString& outWords);

	/** CHANGE ALL over Search:. outChanged / outMatched = the places written / found. False = it did not run - outWords says why. */
	bool ChangeAll(int32 mode, const KFCTableLook& look, int32& outChanged, int32& outMatched, PMString& outWords);
}

#endif // __KFCFaceText_h__

// End, KFCFaceText.h.
