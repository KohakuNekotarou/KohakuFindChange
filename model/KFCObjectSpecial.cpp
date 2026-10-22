//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  An object row's other Special Operations - see KFCObjectSpecial.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IArrangeFacade.h"					// Object > Arrange
#include "IDataBase.h"
#include "IDocument.h"						// GetDocWorkSpace - Clear Object Style Overrides
#include "IDocumentLayer.h"					// Move to Active Layer's layer
#include "IFrameContentFacade.h"			// Object > Fitting
#include "IGeometryFacade.h"				// GetItemBounds - an item's and its spread's bounds (BeyondPasteboard)
#include "IHierarchy.h"
#include "ILayerFacade.h"					// MoveToLayer - the Layers panel's own (layerpanel/LayerSuiteLayoutCSB.cpp)
#include "IObjectStylesFacade.h"			// ApplyObjectStyleToUIDList - the Object Styles panel's Clear Overrides
#include "IObjStylesAttrCategoriesList.h"	// GetApplyCategories - the categories a style applies
#include "IPageItemLockFacade.h"			// Object > Lock / Unlock
#include "IPageItemScrapFacade.h"			// Edit > Clear (Delete)
#include "IPageItemVisibilityFacade.h"		// Object > Hide / Show
#include "IPersistUIDData.h"				// IID_IPERSISTOBJECTSTYLEDATA - the object style applied to an item
#include "ISpreadLayer.h"					// GetDocLayerUID - the layer an item at the top of a spread layer is on
#include "ITransformFacade.h"				// Object > Transform
#include "ITransformPanelPrefs.h"			// the Transform panel's settings InDesign's own transforms read
#include "IWorkspace.h"

// General includes:
#include "ErrorUtils.h"
#include "GenericID.h"						// IID_IPERSISTOBJECTSTYLEDATA
#include "K2Vector.h"
#include "PreferenceUtils.h"				// QuerySessionPreferences
#include "TransformTypes.h"
#include "UIDList.h"
#include "Utils.h"

#include <algorithm>		// std::remove_if - the rows written taken off the rows the write moves
#include <cmath>			// std::isfinite - a transform's result measured (BeyondPasteboard)
#include <map>
#include <set>
#include <utility>
#include <vector>

// Project includes:
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCDiagWrittenDocs.h"		// a test build's written-document check (nothing in a shipping one)
#include "KFCID.h"					// the Edit menu's names of the steps
#include "KFCObjectReplace.h"		// RowsTheWriteCanMove, FollowRowsTheWriteMoved - the other rows a write moves;
									// SayItemsWritten - what a write of object rows says
#include "KFCObjectSearch.h"		// IsOnSpread, Fingerprint, ReadItemPlace
#include "KFCObjectSpecial.h"
#include "KFCPageItemFacts.h"		// IsPageItemLockedForEdit, IsFrameOnLockedLayer, StackIndex, ItemIsThere
#include "KFCReplaceEngine.h"		// the write's undo step (WriteStep) and what its documents are left as (DocsAfterWrite)
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCUndoFollow.h"

namespace
{
// HOW AN OPERATION WRITES (the spec's E5).
enum WriteKind
{
	kWriteBatch = 0,	// one Facade call over a document's items - as InDesign's own command over a selection of them
	kWriteEach,			// one call per item, about its own centre - a transform (a selection's centre means nothing
						// across spreads)
	kWriteGone			// a batch that takes the items away (Delete): the row reads Deleted, its Replace greyed
};

// ONE OPERATION OF THE TABLE: what it is called, how it writes, and what is said when InDesign's own question says no.
struct OpInfo
{
	int32		op;				// a KFCSpecialOp
	const char*	name;			// InDesign's English name - the head of the status line ("Fit Content Proportionally: ...")
	const char*	stepKey;		// the Edit menu's name of its undo step (a key - KFC_enUS.fr)
	WriteKind	kind;
	bool		order;			// its write moves no fingerprint, only the item's place in its stack (Arrange - E6)
	const char*	notApplicable;	// InDesign's own question said no: the words a count of such rows reads
};

const OpInfo kOps[] =
{
	{ kKFCSpecialFillFrameProp,			"Fill Frame Proportionally",			kKFCFillFramePropStepKey,		kWriteBatch,	false,	"no content to fit" },
	{ kKFCSpecialFitContentProp,		"Fit Content Proportionally",			kKFCFitContentPropStepKey,		kWriteBatch,	false,	"no content to fit" },
	{ kKFCSpecialContentAwareFit,		"Content-Aware Fit",					kKFCContentAwareFitStepKey,		kWriteBatch,	false,	"not an image frame" },
	{ kKFCSpecialFitFrameToContent,		"Fit Frame to Content",					kKFCFrameToContentStepKey,		kWriteBatch,	false,	"no content to fit to" },
	{ kKFCSpecialFitContentToFrame,		"Fit Content to Frame",					kKFCFitContentToFrameStepKey,	kWriteBatch,	false,	"no content to fit" },
	{ kKFCSpecialCenterContent,			"Center Content",						kKFCCenterContentStepKey,		kWriteBatch,	false,	"no content to center" },
	{ kKFCSpecialBringToFront,			"Bring to Front",						kKFCBringFrontStepKey,			kWriteBatch,	true,	"in front already" },
	{ kKFCSpecialBringForward,			"Bring Forward",						kKFCBringForwardStepKey,		kWriteBatch,	true,	"in front already" },
	{ kKFCSpecialSendBackward,			"Send Backward",						kKFCSendBackwardStepKey,		kWriteBatch,	true,	"at the back already" },
	{ kKFCSpecialSendToBack,			"Send to Back",							kKFCSendToBackStepKey,			kWriteBatch,	true,	"at the back already" },
	{ kKFCSpecialRotate90CW,			"Rotate 90 Degrees Clockwise",			kKFCRotate90CWStepKey,			kWriteEach,		false,	"cannot be rotated" },
	{ kKFCSpecialRotate90CCW,			"Rotate 90 Degrees Counter-clockwise",	kKFCRotate90CCWStepKey,			kWriteEach,		false,	"cannot be rotated" },
	{ kKFCSpecialRotate180,				"Rotate 180 Degrees",					kKFCRotate180StepKey,			kWriteEach,		false,	"cannot be rotated" },
	{ kKFCSpecialFlipHorizontal,		"Flip Horizontal",						kKFCFlipHorizontalStepKey,		kWriteEach,		false,	"cannot be flipped" },
	{ kKFCSpecialFlipVertical,			"Flip Vertical",						kKFCFlipVerticalStepKey,		kWriteEach,		false,	"cannot be flipped" },
	{ kKFCSpecialRedefineScaling,		"Redefine Scaling as 100%",				kKFCRedefineScalingStepKey,		kWriteBatch,	false,	"at 100% already" },
	{ kKFCSpecialClearTransformations,	"Clear Transformations",				kKFCClearTransformationsStepKey,	kWriteBatch,	false,	"not transformed" },
	{ kKFCSpecialLock,					"Lock",									kKFCLockStepKey,				kWriteBatch,	false,	"cannot be locked" },
	{ kKFCSpecialUnlock,				"Unlock",								kKFCUnlockStepKey,				kWriteBatch,	false,	"cannot be unlocked" },
	{ kKFCSpecialHide,					"Hide",									kKFCHideStepKey,				kWriteBatch,	false,	"cannot be hidden" },
	{ kKFCSpecialShow,					"Show",									kKFCShowStepKey,				kWriteBatch,	false,	"cannot be shown" },
	{ kKFCSpecialMoveToLayer,			"Move to Active Layer",					kKFCMoveToLayerStepKey,			kWriteBatch,	false,	"cannot be moved" },
	{ kKFCSpecialClearStyleOverrides,	"Clear Object Style Overrides",			kKFCClearStyleOverridesStepKey,	kWriteBatch,	false,	"no object style" },
	{ kKFCSpecialDelete,				"Delete",								kKFCDeleteStepKey,				kWriteGone,		false,	"cannot be deleted" },
	{ kKFCSpecialMove,					"Move",									kKFCMoveStepKey,				kWriteEach,		false,	"cannot be moved" },
	{ kKFCSpecialScale,					"Scale",								kKFCScaleStepKey,				kWriteEach,		false,	"cannot be scaled" },
	{ kKFCSpecialRotate,				"Rotate",								kKFCRotateStepKey,				kWriteEach,		false,	"cannot be rotated" },
	{ kKFCSpecialShear,					"Shear",								kKFCShearStepKey,				kWriteEach,		false,	"cannot be sheared" },
};

const OpInfo* FindOp(int32 op)
{
	for (size_t k = 0; k < sizeof(kOps) / sizeof(kOps[0]); ++k)
		if (kOps[k].op == op)
			return &kOps[k];
	return nil;
}

// WHY A ROW IS NOT WRITTEN - said in full for one row (RowSentence), counted for several (RefusalWords).
enum Refusal
{
	kRefusedNone = 0,
	kRefusedNotObject,		// not an object row (never from the panel - the menu is the object rows')
	kRefusedNoDoc,			// its document could not be found
	kRefusedNotOpened,		// ...or not opened
	kRefusedMissing,		// the item is gone - the row now reads Missing
	kRefusedOverset,		// no spread holds it - in overset text (KFCObjectSearch::IsOnSpread)
	kRefusedLockedLayer,	// on a locked layer - every operation, Unlock too (it frees Object > Lock, not a layer)
	kRefusedLocked,			// locked by Object > Lock - every operation but Unlock
	kRefusedNotLocked,		// Unlock: nothing of Object > Lock on it
	kRefusedHidden,			// Hide: hidden already (its own flag)
	kRefusedNotHidden,		// Show: not hidden by Object > Hide
	kRefusedNested,			// Move to Active Layer: in a group or in text - its group or its story moves it
	kRefusedNoLayer,		// ...its document has no layer of that name
	kRefusedOnLayer,		// ...on that layer already
	kRefusedBeyondPasteboard,	// a transform would take it a pasteboard beyond its spread's (BeyondPasteboard)
	kRefusedNotApplicable,	// InDesign's own question said no (OpInfo::notApplicable)
	kRefusedUnchanged,		// written, and InDesign changed nothing of it (E7 - not marked)
	kRefusalCount
};

// One row's refusal, after "<operation>: ".
PMString RowSentence(const OpInfo& info, Refusal why, const KFCSpecialParams& params)
{
	PMString out;
	out.SetTranslatable(kFalse);
	switch (why)
	{
		case kRefusedNotObject:		out = "this row is not an object row."; break;
		case kRefusedNoDoc:			out = "the document of this row could not be found."; break;
		case kRefusedNotOpened:		out = "the document of this row could not be opened."; break;
		case kRefusedMissing:		out = "the object is no longer in the document - search again."; break;
		case kRefusedOverset:		out = "the object is in overset text - left as it is."; break;
		case kRefusedLockedLayer:	out = "the object is on a locked layer - left as it is."; break;
		case kRefusedLocked:		out = "the object is locked (Object > Lock) - left as it is."; break;
		case kRefusedNotLocked:		out = "the object is not locked (Object > Lock)."; break;
		case kRefusedHidden:		out = "the object is hidden already."; break;
		case kRefusedNotHidden:		out = "the object is not hidden (Object > Hide)."; break;
		case kRefusedNested:		out = "the object is in a group or in text - move its group or its frame instead."; break;
		case kRefusedNoLayer:
			out = "the document of this row has no layer named \"";
			out.Append(params.text);
			out.Append("\".");
			break;
		case kRefusedOnLayer:
			out = "the object is on \"";
			out.Append(params.text);
			out.Append("\" already.");
			break;
		case kRefusedBeyondPasteboard:	out = "the object would go beyond the pasteboard - left as it is."; break;
		case kRefusedNotApplicable:
			out = info.notApplicable;
			out.Append(" - left as it is.");
			break;
		case kRefusedUnchanged:		out = "nothing changed - the object is already as asked."; break;
		default:					break;
	}
	out.SetTranslatable(kFalse);
	return out;
}

// The same reasons, a few words each, for a count of rows: "2 locked".
PMString RefusalWords(const OpInfo& info, Refusal why, const KFCSpecialParams& params)
{
	PMString out;
	switch (why)
	{
		case kRefusedNotObject:		out = "not an object row"; break;
		case kRefusedNoDoc:			out = "document not found"; break;
		case kRefusedNotOpened:		out = "document not opened"; break;
		case kRefusedMissing:		out = "no longer in the document"; break;
		case kRefusedOverset:		out = "in overset text"; break;
		case kRefusedLockedLayer:	out = "on a locked layer"; break;
		case kRefusedLocked:		out = "locked"; break;
		case kRefusedNotLocked:		out = "not locked"; break;
		case kRefusedHidden:		out = "hidden already"; break;
		case kRefusedNotHidden:		out = "not hidden"; break;
		case kRefusedNested:		out = "in a group or in text"; break;
		case kRefusedNoLayer:
			out = "no layer named \"";
			out.Append(params.text);
			out.Append("\"");
			break;
		case kRefusedOnLayer:
			out = "on \"";
			out.Append(params.text);
			out.Append("\" already");
			break;
		case kRefusedBeyondPasteboard:	out = "beyond the pasteboard"; break;
		case kRefusedNotApplicable:	out = info.notApplicable; break;
		case kRefusedUnchanged:		out = "unchanged"; break;
		default:					break;
	}
	out.SetTranslatable(kFalse);
	return out;
}

// "1 locked, 2 unchanged" - the reasons counted, in the order of the enum.
void AppendRefusals(PMString& out, const OpInfo& info, const KFCSpecialParams& params, const int32 (&counts)[kRefusalCount])
{
	bool first = true;
	for (int32 why = kRefusedNotObject; why < kRefusalCount; ++why)
	{
		if (counts[why] == 0)
			continue;
		if (!first)
			out.Append(", ");
		out.AppendNumber(counts[why]);
		out.Append(" ");
		out.Append(RefusalWords(info, static_cast<Refusal>(why), params));
		first = false;
	}
}

// A refusal begins with the operation's name (Create Outlines' rule - KFCObjectOutline.cpp).
void Refuse(PMString& out, const OpInfo& info, const PMString& why)
{
	out = info.name;
	out.Append(": ");
	out.Append(why);
	out.SetTranslatable(kFalse);
}

// The object style applied to an item (kInvalidUID = none).
UID StyleOf(IDataBase* db, UID item)
{
	InterfacePtr<IPersistUIDData> style(db, item, IID_IPERSISTOBJECTSTYLEDATA);
	return (style != nil) ? style->GetUID() : kInvalidUID;
}

// THE TRANSFORM PANEL'S SETTINGS, as InDesign's own transforms read them (the session's ITransformPanelPrefs, read as
// publiclib's CPathCreationTracker reads it). Measured against InDesign's own: with Dimensions Include Stroke Weight
// off, its Rotate 90 Degrees Clockwise turns an item about its path's centre, not its stroke's; with Transformations
// are Totals off, its Clear Transformations clears an item in a group in the group's coordinates - its Move, Flip and
// Shear stay in the pasteboard's either way.
// The bounds an item's centre is taken from (TransformEach).
Geometry::BoundsKind CentreBounds()
{
	Geometry::BoundsKind bounds = Geometry::PathBounds();
	InterfacePtr<ITransformPanelPrefs> prefs(QuerySessionPreferences<ITransformPanelPrefs>());
	if (prefs != nil)
		bounds = prefs->GetShowStrokeBounds();
	return bounds;
}

// The coordinates Clear Transformations clears in.
Transform::CoordinateSpace ClearSpace()
{
	Transform::CoordinateSpace space = Transform::PasteboardCoordinates();
	InterfacePtr<ITransformPanelPrefs> prefs(QuerySessionPreferences<ITransformPanelPrefs>());
	if (prefs != nil)
		space = prefs->GetCoordinateSpace();
	return space;
}

// MOVE TO ACTIVE LAYER'S DOORS (E11): an item at the top of a spread layer - one in a group or in text goes with its group
// or its story (InDesign's own moves the selection's outermost items - LayerSuiteLayoutCSB.cpp); its document's layer of
// that name, not the one it is on. With no name (the menu's question - CanSpecialHit), the first door alone.
Refusal CheckLayerMove(const KFCSpecialParams& params, const UIDRef& docRef, UID item)
{
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const UID parent = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
	if (parent == kInvalidUID)
		return kRefusedNested;
	InterfacePtr<ISpreadLayer> spreadLayer(db, parent, UseDefaultIID());
	if (spreadLayer == nil)
		return kRefusedNested;
	if (params.text.IsEmpty())
		return kRefusedNone;
	const UID target = KFCPageItemFacts::DocLayerNamed(docRef, params.text);
	if (target == kInvalidUID)
		return kRefusedNoLayer;
	return (spreadLayer->GetDocLayerUID() == target) ? kRefusedOnLayer : kRefusedNone;
}

// ARRANGE'S QUESTION, OF A SPREAD'S ITEMS TOGETHER - as InDesign asks it of a selection: the items move together, their
// order among themselves kept, so an item already at the back stays among them. (Asked one item at a time, the backmost of
// four was left out "at the back already" and the other three went behind it, where InDesign's own Send to Back over the
// four leaves them as they are - measured live, 2026-10-17 night.)
bool CanArrange(int32 op, const UIDList& items)
{
	Utils<Facade::IArrangeFacade> arrange;
	if (!arrange)
		return false;
	switch (op)
	{
		case kKFCSpecialBringToFront:	return arrange->CanBringToFront(items, kInvalidUID) != kFalse;
		case kKFCSpecialBringForward:	return arrange->CanBringForward(items) != kFalse;
		case kKFCSpecialSendBackward:	return arrange->CanSendBackward(items) != kFalse;
		case kKFCSpecialSendToBack:		return arrange->CanSendToBack(items, kInvalidUID) != kFalse;
		default:						return false;
	}
}

// INDESIGN'S OWN QUESTION FOR THE OPERATION, of one item (E4) - its Can..., the one its menu item is enabled by. forWrite:
// asked for the write, where an Arrange row is not asked alone - its spread's items are asked together (CanArrange, in the
// write); the menu's grey asks each row.
Refusal AskInDesign(const OpInfo& info, const KFCSpecialParams& params, const UIDRef& docRef, UID item, bool forWrite)
{
	IDataBase* const db = docRef.GetDataBase();
	UIDList one(db, item);
	bool can = false;
	switch (info.op)
	{
		case kKFCSpecialFillFrameProp:
		case kKFCSpecialFitContentProp:
		case kKFCSpecialContentAwareFit:
		case kKFCSpecialFitFrameToContent:
		case kKFCSpecialFitContentToFrame:
		case kKFCSpecialCenterContent:
		{
			Utils<Facade::IFrameContentFacade> fitting;
			if (!fitting)
				break;
			if (info.op == kKFCSpecialFillFrameProp)			can = fitting->CanFillFrameProp(one) != kFalse;
			else if (info.op == kKFCSpecialFitContentProp)		can = fitting->CanFitContentProp(one) != kFalse;
			else if (info.op == kKFCSpecialContentAwareFit)		can = fitting->CanApplyContentAwareFit(one) != kFalse;
			else if (info.op == kKFCSpecialFitFrameToContent)	can = fitting->CanFitFrameToContent(one) != kFalse;
			else if (info.op == kKFCSpecialFitContentToFrame)	can = fitting->CanFitContentToFrame(one) != kFalse;
			else												can = fitting->CanCenterContentInFrame(one) != kFalse;
			break;
		}
		case kKFCSpecialBringToFront:
		case kKFCSpecialBringForward:
		case kKFCSpecialSendBackward:
		case kKFCSpecialSendToBack:
			if (forWrite)
				return kRefusedNone;		// asked with the spread's other items (CanArrange, in the write)
			can = CanArrange(info.op, one);
			break;
		case kKFCSpecialRotate90CW:
		case kKFCSpecialRotate90CCW:
		case kKFCSpecialRotate180:
		case kKFCSpecialRotate:
		case kKFCSpecialFlipHorizontal:
		case kKFCSpecialFlipVertical:
		case kKFCSpecialScale:
		case kKFCSpecialShear:
		case kKFCSpecialMove:
		{
			Utils<Facade::ITransformFacade> transform;
			if (!transform)
				break;
			uint32 values = Transform::kRotationValue;
			if (info.op == kKFCSpecialFlipHorizontal || info.op == kKFCSpecialFlipVertical || info.op == kKFCSpecialScale)
				values = Transform::kScaleValue;
			else if (info.op == kKFCSpecialShear)
				values = Transform::kSkewValue;
			else if (info.op == kKFCSpecialMove)
				values = Transform::kTranslationValue;
			can = transform->CanChangeItemsTransformValues(one, Transform::PasteboardCoordinates(), values) != kFalse;
			break;
		}
		case kKFCSpecialRedefineScaling:
		{
			Utils<Facade::ITransformFacade> transform;
			can = transform && transform->CanRedefineItemsScaling(one) != kFalse;
			break;
		}
		case kKFCSpecialClearTransformations:
		{
			Utils<Facade::ITransformFacade> transform;
			can = transform && transform->CanRemoveItemsTransformations(one, ClearSpace()) != kFalse;
			break;
		}
		case kKFCSpecialLock:
		{
			Utils<Facade::IPageItemLockFacade> lock;
			can = lock && lock->CanLock(one) != kFalse;
			break;
		}
		case kKFCSpecialUnlock:
		{
			Utils<Facade::IPageItemLockFacade> lock;
			return (lock && lock->CanUnlock(one) != kFalse) ? kRefusedNone : kRefusedNotLocked;
		}
		case kKFCSpecialHide:
		{
			Utils<Facade::IPageItemVisibilityFacade> visibility;
			if (!visibility)
				break;
			if (visibility->GetVisibleState(UIDRef(db, item)) == kFalse)
				return kRefusedHidden;
			can = visibility->CanAnyHide(one) != kFalse;
			break;
		}
		case kKFCSpecialShow:
		{
			// Its own flag (GetVisibleState - "does not go up the hierarchy"): what Object > Hide set and Show takes off.
			Utils<Facade::IPageItemVisibilityFacade> visibility;
			return (visibility && visibility->GetVisibleState(UIDRef(db, item)) == kFalse) ? kRefusedNone : kRefusedNotHidden;
		}
		case kKFCSpecialMoveToLayer:
			return CheckLayerMove(params, docRef, item);
		case kKFCSpecialClearStyleOverrides:
			can = StyleOf(db, item) != kInvalidUID;
			break;
		case kKFCSpecialDelete:
		{
			Utils<Facade::IPageItemScrapFacade> scrap;
			can = scrap && scrap->CanDeletePageItems(one) != kFalse;
			break;
		}
		default:
			break;
	}
	return can ? kRefusedNone : kRefusedNotApplicable;
}

// THE DOORS FOR ONE ROW (the spec's E4). reach = the write's: the row's document reached (a closed chapter reopened
// windowless, held), the row rebound to it, and a row whose item is gone marked Missing. Else the menu's, READ ONLY: nothing
// reached or opened, a document not open now answers "can" (pressing reaches it). outDoc / outItem: the row's document and
// its item.
Refusal CheckItem(const OpInfo& info, const KFCSpecialParams& params, int32 chapterIdx, int32 hitIdx, bool reach,
	UIDRef& outDoc, UID& outItem)
{
	outItem = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	if (outItem == kInvalidUID)
		return kRefusedNotObject;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDoc, file))
		return kRefusedNoDoc;
	if (reach)
	{
		if (!KFCBookScope::ReachChapterDoc(file, outDoc))
			return kRefusedNotOpened;
		KFCResultModel::RebindChapterDoc(chapterIdx, outDoc);
	}
	else if (!KFCBookScope::FindOpenChapterDoc(file, outDoc))
		return kRefusedNone;
	IDataBase* const db = outDoc.GetDataBase();
	if (!KFCResultModel::RowItemIsThere(chapterIdx, hitIdx, db))
	{
		if (reach)
			KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeMissing);
		return kRefusedMissing;
	}
	if (!KFCObjectSearch::IsOnSpread(UIDRef(db, outItem)))
		return kRefusedOverset;
	// LOCKED: a locked layer refuses every operation - Unlock frees what Object > Lock locked, not a layer; Object > Lock
	// refuses all but Unlock, which asks for it (AskInDesign).
	if (KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
		return kRefusedLockedLayer;
	if (info.op != kKFCSpecialUnlock && KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem))
		return kRefusedLocked;
	return AskInDesign(info, params, outDoc, outItem, reach);
}

// SHEAR ON ITS AXIS - the Shear dialog's Axis, as InDesign's own (KFCSpecialParams::y: 0 Horizontal, 1 Vertical). Horizontal
// is Transform::SkewBy; Vertical skews the other way round - the x left as it is, the y moved with it (PMMatrix's
// SkewVertical), with SkewBy's own sign for the left-handed coordinates (TransformTypes.h: SkewHorizontal(-degrees)). Its
// direction was measured against InDesign's own Shear dialog with the Vertical axis (the live test of 2026-10-17).
Transform::TransformValue ShearBy(const KFCSpecialParams& params)
{
	if (params.y < 0.5)
		return Transform::SkewBy(params.x);
	PMMatrix m;
	m.SkewVertical(-params.x);
	return Transform::TransformBy(m);
}

// THE TRANSFORM OF AN OPERATION THAT WRITES EACH ITEM (kWriteEach) - the write's (TransformEach) and the measure of its
// result (BeyondPasteboard). Transform::RotateBy takes the angle as InDesign's dialogs do - counter-clockwise positive.
Transform::TransformValue TransformOf(const OpInfo& info, const KFCSpecialParams& params)
{
	switch (info.op)
	{
		case kKFCSpecialRotate90CW:		return Transform::RotateBy(-90.0);
		case kKFCSpecialRotate90CCW:	return Transform::RotateBy(90.0);
		case kKFCSpecialRotate180:		return Transform::RotateBy(180.0);
		case kKFCSpecialFlipHorizontal:	return Transform::ScaleBy(-1.0, 1.0);
		case kKFCSpecialFlipVertical:	return Transform::ScaleBy(1.0, -1.0);
		case kKFCSpecialMove:			return Transform::TranslateBy(params.x, params.y);
		case kKFCSpecialScale:			return Transform::ScaleBy(params.x / 100.0, params.y / 100.0);
		case kKFCSpecialRotate:			return Transform::RotateBy(params.x);
		case kKFCSpecialShear:			return ShearBy(params);
		default:						return Transform::TranslateBy(0.0, 0.0);	// (no operation of this table)
	}
}

// BEYOND THE PASTEBOARD: would the transform take the item more than a pasteboard beyond its spread's pasteboard? Its
// bounds as they stand (pasteboard coordinates - the centre's bounds) transformed about their centre, against the
// spread's bounds grown by their own width and height on each side. InDesign's own trackers keep a drag inside the
// spread's bounds (CLayoutTracker, CPathCreationTracker) and its Move and Scale dialogs warn of objects lost from the
// pasteboard and too large - let past that warning, a Scale of 999999999% took InDesign down (measured); so a value out
// of all measure, or one a dialog read as infinite, is refused here, before anything is written. Not finite = beyond.
bool BeyondPasteboard(IDataBase* db, UID item, const Transform::TransformValue& how, const Geometry::BoundsKind& bounds)
{
	Utils<Facade::IGeometryFacade> geometry;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const UID spread = (hier != nil) ? hier->GetSpreadUID() : kInvalidUID;
	if (!geometry || spread == kInvalidUID)
		return false;		// (nothing to measure by - InDesign's own refusals are left to stop it)
	const PMRect board = geometry->GetItemBounds(UIDRef(db, spread), Transform::PasteboardCoordinates(),
		Geometry::OuterStrokeBounds());
	const PMRect now = geometry->GetItemBounds(UIDRef(db, item), Transform::PasteboardCoordinates(), bounds);
	const PMReal cx = (now.Left() + now.Right()) / 2;
	const PMReal cy = (now.Top() + now.Bottom()) / 2;
	PMRect after(now.Left() - cx, now.Top() - cy, now.Right() - cx, now.Bottom() - cy);
	how.transformMatrix.Transform(&after);		// (the smallest upright rectangle holding the transformed one)
	const double left = ::ToDouble(after.Left() + cx);
	const double top = ::ToDouble(after.Top() + cy);
	const double right = ::ToDouble(after.Right() + cx);
	const double bottom = ::ToDouble(after.Bottom() + cy);
	if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom))
		return true;
	const double width = ::ToDouble(board.Width());
	const double height = ::ToDouble(board.Height());
	return left < ::ToDouble(board.Left()) - width || top < ::ToDouble(board.Top()) - height
		|| right > ::ToDouble(board.Right()) + width || bottom > ::ToDouble(board.Bottom()) + height;
}

// TRANSFORM EACH ITEM ABOUT ITS OWN CENTRE (E5) - the official shape (codesnippets/SnpTransformPageItems.cpp:
// ITransformFacade::TransformItems), one item at a time, so each turns or flips where it stands - about the centre of
// the bounds InDesign's own transforms take (CentreBounds). kSuccess, or the error.
ErrorCode TransformEach(const UIDList& items, const Transform::TransformValue& how)
{
	Utils<Facade::ITransformFacade> transform;
	if (!transform)
		return kFailure;
	const Geometry::BoundsKind bounds = CentreBounds();
	for (int32 i = 0; i < items.Length(); ++i)
	{
		const ErrorCode err = transform->TransformItems(UIDList(items.GetDataBase(), items[i]), Transform::PasteboardCoordinates(),
			Transform::CenterLocation(bounds), how);
		if (err != kSuccess)
			return err;
		if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			return ErrorUtils::PMGetGlobalErrorCode();
	}
	return kSuccess;
}

// CLEAR OBJECT STYLE OVERRIDES over one document's items: each item's own style applied again over every category the
// style applies - the Object Styles panel's Clear Overrides (IObjectStylesFacade.h: "clear all overrides means all of the
// active categories in the style"), over items of any spread. Items of one style are applied together.
ErrorCode ClearOverrides(const UIDRef& docRef, const UIDList& items)
{
	IDataBase* const db = docRef.GetDataBase();
	Utils<Facade::IObjectStylesFacade> styles;
	InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
	if (!styles || doc == nil)
		return kFailure;
	InterfacePtr<IWorkspace> ws(doc->GetDocWorkSpace(), UseDefaultIID());
	if (ws == nil)
		return kFailure;
	std::map<UID, UIDList> byStyle;
	for (int32 i = 0; i < items.Length(); ++i)
	{
		const UID style = StyleOf(db, items[i]);
		if (style == kInvalidUID)
			continue;
		std::map<UID, UIDList>::iterator at = byStyle.find(style);
		if (at == byStyle.end())
			at = byStyle.insert(std::make_pair(style, UIDList(db))).first;
		at->second.Append(items[i]);
	}
	for (std::map<UID, UIDList>::const_iterator s = byStyle.begin(); s != byStyle.end(); ++s)
	{
		InterfacePtr<IObjStylesAttrCategoriesList> categoriesOf(db, s->first, UseDefaultIID());
		if (categoriesOf == nil)
			continue;
		K2Vector<ClassID> categories(categoriesOf->GetApplyCategories());
		const ErrorCode err = styles->ApplyObjectStyleToUIDList(s->second, ws, s->first, categories, kFalse);
		if (err != kSuccess)
			return err;
		if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			return ErrorUtils::PMGetGlobalErrorCode();
	}
	return kSuccess;
}

// THE WRITE OF ONE SPREAD'S ITEMS (E5 - SpreadGroups): InDesign's own Facade over them - one call over them all, as over a
// selection of them; a transform one call per item about its own centre. kSuccess, or the error.
ErrorCode WriteDocument(const OpInfo& info, const KFCSpecialParams& params, const UIDRef& docRef, const UIDList& items)
{
	switch (info.op)
	{
		case kKFCSpecialFillFrameProp:
		case kKFCSpecialFitContentProp:
		case kKFCSpecialContentAwareFit:
		case kKFCSpecialFitFrameToContent:
		case kKFCSpecialFitContentToFrame:
		case kKFCSpecialCenterContent:
		{
			Utils<Facade::IFrameContentFacade> fitting;
			if (!fitting)
				return kFailure;
			UIDList list(items);
			if (info.op == kKFCSpecialFillFrameProp)			return fitting->FillFrameProp(list);
			if (info.op == kKFCSpecialFitContentProp)			return fitting->FitContentProp(list);
			if (info.op == kKFCSpecialContentAwareFit)			return fitting->ApplyContentAwareFit(list);
			if (info.op == kKFCSpecialFitFrameToContent)		return fitting->FitFrameToContent(list);
			if (info.op == kKFCSpecialFitContentToFrame)		return fitting->FitContentToFrame(list);
			return fitting->CenterContentInFrame(list);
		}
		case kKFCSpecialBringToFront:
		case kKFCSpecialBringForward:
		case kKFCSpecialSendBackward:
		case kKFCSpecialSendToBack:
		{
			Utils<Facade::IArrangeFacade> arrange;
			if (!arrange)
				return kFailure;
			if (info.op == kKFCSpecialBringToFront)		return arrange->ProcessBringToFront(items, kInvalidUID, kFalse);
			if (info.op == kKFCSpecialBringForward)		return arrange->ProcessBringForward(items);
			if (info.op == kKFCSpecialSendBackward)		return arrange->ProcessSendBackward(items);
			return arrange->ProcessSendToBack(items, kInvalidUID, kFalse);
		}
		case kKFCSpecialRotate90CW:
		case kKFCSpecialRotate90CCW:
		case kKFCSpecialRotate180:
		case kKFCSpecialFlipHorizontal:
		case kKFCSpecialFlipVertical:
		case kKFCSpecialMove:
		case kKFCSpecialScale:
		case kKFCSpecialRotate:
		case kKFCSpecialShear:
			return TransformEach(items, TransformOf(info, params));
		case kKFCSpecialRedefineScaling:
		{
			Utils<Facade::ITransformFacade> transform;
			return transform ? transform->RedefineItemsScaling(items) : kFailure;
		}
		case kKFCSpecialClearTransformations:
		{
			Utils<Facade::ITransformFacade> transform;
			return transform ? transform->RemoveItemsTransformations(items, ClearSpace()) : kFailure;
		}
		case kKFCSpecialLock:
		case kKFCSpecialUnlock:
		{
			Utils<Facade::IPageItemLockFacade> lock;
			if (!lock)
				return kFailure;
			return (info.op == kKFCSpecialLock) ? lock->LockPageItems(items) : lock->UnlockPageItems(items);
		}
		case kKFCSpecialHide:
		case kKFCSpecialShow:
		{
			Utils<Facade::IPageItemVisibilityFacade> visibility;
			if (!visibility)
				return kFailure;
			return (info.op == kKFCSpecialHide) ? visibility->Hide(items) : visibility->Show(items);
		}
		case kKFCSpecialMoveToLayer:
		{
			Utils<Facade::ILayerFacade> layers;
			const UID targetUID = KFCPageItemFacts::DocLayerNamed(docRef, params.text);
			if (!layers || targetUID == kInvalidUID)
				return kFailure;
			InterfacePtr<IDocumentLayer> target(docRef.GetDataBase(), targetUID, UseDefaultIID());
			if (target == nil)
				return kFailure;
			UIDList moving(items);		// (in and out: the items moved - the same ones, nothing duplicated)
			return layers->MoveToLayer(moving, target, kFalse);
		}
		case kKFCSpecialClearStyleOverrides:
			return ClearOverrides(docRef, items);
		case kKFCSpecialDelete:
		{
			Utils<Facade::IPageItemScrapFacade> scrap;
			return scrap ? scrap->DeletePageItems(items) : kFailure;
		}
		default:
			return kFailure;
	}
}

// THE ITEMS BY THEIR SPREAD, in the order the rows list them - the unit InDesign's own commands take: a selection is one
// spread's, and a Facade handed items of two spreads at once can act as if they were one's (ILayerFacade::MoveToLayer put
// the second spread's items on the first - measured live, 2026-10-17).
void SpreadGroups(IDataBase* db, const std::vector<std::pair<int32, UID> >& todo, std::vector<UIDList>& out)
{
	out.clear();
	std::vector<UID> spreads;
	for (size_t k = 0; k < todo.size(); ++k)
	{
		InterfacePtr<IHierarchy> hier(db, todo[k].second, UseDefaultIID());
		const UID spread = (hier != nil) ? hier->GetSpreadUID() : kInvalidUID;
		size_t s = 0;
		while (s < spreads.size() && spreads[s] != spread)
			++s;
		if (s == spreads.size())
		{
			spreads.push_back(spread);
			out.push_back(UIDList(db));
		}
		out[s].Append(todo[k].second);
	}
}

// ONE DOCUMENT'S ROWS in the write: those that passed the doors, with their items as they read before it, and what the
// write changed of them.
struct SpecialChapter
{
	int32								chapterIdx;
	UIDRef								docRef;			// reached by CheckItem (a closed chapter reopened windowless, held)
	std::vector<std::pair<int32, UID> >	todo;			// (the row, its item)
	std::vector<uint64>					prints;			// each todo item's fingerprint before the write...
	std::vector<uint32>					printLengths;
	std::vector<int32>					orders;			// ...and its place in its stack (Arrange - E6)
	std::vector<size_t>					changed;		// the todo entries the write changed (E7)
	std::set<UID>						changedItems;
	KFCObjectReplace::RowItems			watched;		// the OTHER rows its write can move (RowsTheWriteCanMove)
	SpecialChapter() : chapterIdx(-1) {}
};

// DID THE WRITE CHANGE THE ITEM (E7): gone (Delete), or its fingerprint moved - or, for Arrange, its place in its stack.
bool WriteChanged(const OpInfo& info, IDataBase* db, UID item, uint64 printBefore, uint32 lengthBefore, int32 orderBefore)
{
	const bool there = KFCPageItemFacts::ItemIsThere(db, item);
	if (info.kind == kWriteGone)
		return !there;
	if (!there)
		return false;
	uint64 print = 0;
	uint32 length = 0;
	if (!KFCObjectSearch::Fingerprint(UIDRef(db, item), print, length))
		return false;
	if (print != printBefore || length != lengthBefore)
		return true;
	return info.order && KFCPageItemFacts::StackIndex(db, item) != orderBefore;
}
}	// anonymous namespace

bool KFCObjectSpecial::CanSpecialHit(int32 op, int32 chapterIdx, int32 hitIdx)
{
	const OpInfo* const info = FindOp(op);
	if (info == nil)
		return false;
	UIDRef docRef;
	UID item = kInvalidUID;
	return CheckItem(*info, KFCSpecialParams(), chapterIdx, hitIdx, false, docRef, item) == kRefusedNone;
}

bool KFCObjectSpecial::SpecialRows(int32 op, const KFCRowsByChapter& rows, const KFCSpecialParams& params, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("object-special-rows");	// (test builds: when it is over - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	const OpInfo* const found = FindOp(op);
	if (found == nil)
		return false;
	const OpInfo& info = *found;
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const KFCReplaceEngine::WriteUnderWay writing;		// counted from the door to the hand-back (KFCRunGuard)
	const size_t total = KFCRowCount(rows);
	if (total == 0)
		return false;

	// THE DOORS, ROW BY ROW: the rows that pass are written, the rest counted by their reason.
	int32 counts[kRefusalCount] = {};
	Refusal lastWhy = kRefusedNone;
	// A TRANSFORM'S RESULT is measured at the doors, before anything is written (BeyondPasteboard) - its transform and
	// the bounds its centre is taken from, once.
	const Transform::TransformValue how = TransformOf(info, params);
	const Geometry::BoundsKind centreBounds = CentreBounds();
	std::vector<SpecialChapter> chapters(rows.size());
	KFCReplaceEngine::DocsAfterWrite docsAfter;		// what each document is left as, on every way out from here
	size_t toWrite = 0;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		SpecialChapter& chapter = chapters[c];
		chapter.chapterIdx = rows[c].chapter;
		for (size_t k = 0; k < rows[c].hits.size(); ++k)
		{
			UID item = kInvalidUID;
			Refusal why = CheckItem(info, params, chapter.chapterIdx, rows[c].hits[k], true, chapter.docRef, item);
			if (why == kRefusedNone && info.kind == kWriteEach
				&& BeyondPasteboard(chapter.docRef.GetDataBase(), item, how, centreBounds))
				why = kRefusedBeyondPasteboard;
			if (why != kRefusedNone)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.todo.push_back(std::make_pair(rows[c].hits[k], item));
		}
		toWrite += chapter.todo.size();
		// Its "unsaved" flag read now when it has rows to write (a document whose flag was not read is never made clean).
		if (chapter.docRef.GetDataBase() != nil)
			docsAfter.Reached(chapter.docRef, !chapter.todo.empty());
	}
	if (toWrite == 0)
	{
		if (total == 1)
			Refuse(outStatus, info, RowSentence(info, lastWhy, params));
		else
		{
			PMString why("none of the ");
			why.AppendNumber(static_cast<int32>(total));
			why.Append(" selected rows can be changed - ");
			AppendRefusals(why, info, params, counts);
			why.Append(".");
			Refuse(outStatus, info, why);
		}
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last row is checked (the object Replace's rule - KFCObjectReplace.cpp).
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (!chapters[c].todo.empty() && !KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			Refuse(outStatus, info, PMString("a document of the selected rows has been closed - nothing was changed."));
			return false;
		}
	}

	// BEFORE THE WRITE: each item as it reads now (what tells the rows it changed - E7), and the other rows of its document
	// the write can move (spec OBJ-12 - the rows written are not among them: their marks are their own).
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		SpecialChapter& chapter = chapters[c];
		if (chapter.todo.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		std::set<UID> items;
		for (size_t k = 0; k < chapter.todo.size(); ++k)
		{
			const UID item = chapter.todo[k].second;
			items.insert(item);
			uint64 print = 0;
			uint32 length = 0;
			(void)KFCObjectSearch::Fingerprint(UIDRef(db, item), print, length);
			chapter.prints.push_back(print);
			chapter.printLengths.push_back(length);
			chapter.orders.push_back(info.order ? KFCPageItemFacts::StackIndex(db, item) : -1);
		}
		KFCObjectReplace::RowsTheWriteCanMove(chapter.chapterIdx, db, items, chapter.watched);
		chapter.watched.erase(std::remove_if(chapter.watched.begin(), chapter.watched.end(),
			[&items](const std::pair<int32, UID>& row) { return items.count(row.second) > 0; }), chapter.watched.end());
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (E6): one step, every item read before the write - an Arrange's with its
	// place in its stack (its print does not move); a Delete's Undo brings the item back, its Redo takes it away again.
	size_t documents = 0;
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].todo.empty())
			continue;
		++documents;
		chapterIdxs.push_back(chapters[c].chapterIdx);
	}
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName(info.name);
	for (size_t c = 0; c < chapters.size(); ++c)
		for (size_t k = 0; k < chapters[c].todo.size(); ++k)
			recorder.RecordItem(chapters[c].chapterIdx, chapters[c].todo[k].second, info.order);
	KFCReplaceEngine::WriteStep step;
	if (!KFCReplaceEngine::BeginWriteStep(documents > 1, step, info.stepKey, "KFC Special Operation"))
	{
		Refuse(outStatus, info, PMString("InDesign would not start a command sequence - nothing was changed."));
		return false;
	}
	// THE WRITE: InDesign's own Facade over each document's items, one call per spread (E5 - a selection's unit:
	// SpreadGroups). An error standing ends the step by it: the whole step goes back, and no row is marked over a write
	// InDesign takes back.
	bool errorStands = false;
	size_t changedCount = 0;
	for (size_t c = 0; c < chapters.size() && !errorStands; ++c)
	{
		SpecialChapter& chapter = chapters[c];
		if (chapter.todo.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		std::vector<UIDList> bySpread;
		SpreadGroups(db, chapter.todo, bySpread);
		ErrorCode done = kSuccess;
		std::set<UID> askedOut;		// an Arrange spread InDesign would not move together: its items, counted here once
		for (size_t s = 0; s < bySpread.size() && done == kSuccess; ++s)
		{
			// ARRANGE: the spread's items asked together, as a selection is (CanArrange) - not one by one.
			if (info.order && !CanArrange(info.op, bySpread[s]))
			{
				counts[kRefusedNotApplicable] += bySpread[s].Length();
				for (int32 i = 0; i < bySpread[s].Length(); ++i)
					askedOut.insert(bySpread[s][i]);
				continue;
			}
			done = WriteDocument(info, params, chapter.docRef, bySpread[s]);
			if (done == kSuccess && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
				done = ErrorUtils::PMGetGlobalErrorCode();
		}
		if (done != kSuccess || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		{
			KFC_DIAG_LOG("SPECIAL op=%d chapter=%d err=%d global=%d", info.op, chapter.chapterIdx, (int)done,
				(int)ErrorUtils::PMGetGlobalErrorCode());
			errorStands = true;
			break;
		}
		// WHAT IT CHANGED (E7), read in the step: an item it left as it was is counted, not marked.
		for (size_t k = 0; k < chapter.todo.size(); ++k)
		{
			if (askedOut.count(chapter.todo[k].second) > 0)
				continue;		// (counted above)
			if (!WriteChanged(info, db, chapter.todo[k].second, chapter.prints[k], chapter.printLengths[k], chapter.orders[k]))
			{
				++counts[kRefusedUnchanged];
				continue;
			}
			chapter.changed.push_back(k);
			chapter.changedItems.insert(chapter.todo[k].second);
		}
		changedCount += chapter.changed.size();
	}
	const bool ok = changedCount > 0 && !errorStands;
	if (ok)
		for (size_t c = 0; c < chapters.size(); ++c)
			if (!chapters[c].changed.empty())
				KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());		// in this step, so its Undo / Redo is heard
	// Kept, or taken back whole when nothing changed or an error stands; the "unsaved" flags are DocsAfterWrite's.
	KFCReplaceEngine::EndWriteStep(step, ok);
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		if (errorStands)
		{
			PMString why("InDesign's ");
			why.Append(info.name);
			why.Append(" stopped with an error - nothing was changed.");
			Refuse(outStatus, info, why);
		}
		else
		{
			PMString why("nothing was changed - ");
			AppendRefusals(why, info, params, counts);
			why.Append(".");
			Refuse(outStatus, info, why);
		}
		return false;
	}
	// E7: each changed row marked - its word, its eye, its place read again, its fingerprint (a Delete's: gone); the other
	// rows the write moved given the prints it left; the documents told.
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const SpecialChapter& chapter = chapters[c];
		if (chapter.changed.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		for (size_t n = 0; n < chapter.changed.size(); ++n)
		{
			const std::pair<int32, UID>& row = chapter.todo[chapter.changed[n]];
			if (info.kind == kWriteGone)
			{
				KFCResultModel::MarkItemSpecial(chapter.chapterIdx, row.first, info.op, 0, 0, nil);
				continue;
			}
			uint64 print = 0;
			uint32 length = 0;
			(void)KFCObjectSearch::Fingerprint(UIDRef(db, row.second), print, length);
			KFCResultModel::Hit place;
			const bool read = KFCObjectSearch::ReadItemPlace(chapter.docRef, row.second, place);
			KFCResultModel::MarkItemSpecial(chapter.chapterIdx, row.first, info.op, print, length, read ? &place : nil);
		}
		KFCObjectReplace::FollowRowsTheWriteMoved(chapter.chapterIdx, db, chapter.watched, chapter.changedItems);
		KFC_DIAG_NOTE_WRITE(chapter.docRef);
		docsAfter.Wrote(chapter.docRef);		// a chapter of ours has to be seen and saved
	}
	recorder.Keep();

	// E8: "<operation>: 3 objects - ID:259, ID:260, ID:261." - the items' IDs (SayItemsWritten), each document's in its
	// rows' order; Move to Active Layer's layer named after the count ("... 5 of 8 objects to "Images" - ...").
	KFCObjectReplace::ItemsWritten items;
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const SpecialChapter& chapter = chapters[c];
		if (chapter.changed.empty())
			continue;
		std::vector<UID> changed;
		for (size_t n = 0; n < chapter.changed.size(); ++n)
			changed.push_back(chapter.todo[chapter.changed[n]].second);
		items.push_back(std::make_pair(chapter.chapterIdx, changed));
	}
	PMString head(info.name);
	head.Append(": ");
	PMString toLayer;
	if (info.op == kKFCSpecialMoveToLayer)
	{
		toLayer = " to \"";
		toLayer.Append(params.text);
		toLayer.Append("\"");
	}
	PMString left;
	AppendRefusals(left, info, params, counts);
	KFCObjectReplace::SayItemsWritten(outStatus, head, changedCount, total, items, toLayer, " - ", left);
	return true;
}

// End, KFCObjectSpecial.cpp.
