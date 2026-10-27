//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A document's QR codes, read - see KFCQRCodes.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocument.h"
#include "IHierarchy.h"
#include "IObjectWalker.h"
#include "IPersistUIDData.h"
#include "IQREmailData.h"
#include "IQRHyperLinkData.h"
#include "IQRPlainTextData.h"
#include "IQRSmsData.h"			// IQRSMSData - the header's name is IQRSmsData.h
#include "IQRvCardData.h"

// General includes:
#include "EPSID.h"						// kEPSItem, IID_IQRCODEDATAUID
#include "ObjectWalkerScopeOptions.h"
#include "TextChar.h"					// kTextChar_CR, kTextChar_LF
#include "TextWalkerServiceProviderID.h"	// kObjectWalkerBoss
#include "WideString.h"

#include <set>

// Project includes:
#include "KFCQRCodes.h"
#include "KFCDiag.h"
#ifdef KFC_DIAG
#include "KFCBookScope.h"				// ActiveDocument - the probe's document
#endif

namespace
{

// The QR code's EPS item for `item` - the item itself, or a frame's one child - or an invalid UIDRef: an EPS item whose
// IID_IQRCODEDATAUID names a data boss (EPSID.h: only kEPSItem carries it).
UIDRef QREpsOf(const UIDRef& item)
{
	IDataBase* const db = item.GetDataBase();
	if (db == nil || item.GetUID() == kInvalidUID || !db->IsValidUID(item.GetUID()))
		return UIDRef();
	UIDRef eps = item;
	if (db->GetClass(item.GetUID()) != kEPSItem)
	{
		InterfacePtr<IHierarchy> hier(item, UseDefaultIID());
		if (hier == nil || hier->GetChildCount() != 1)
			return UIDRef();
		eps = UIDRef(db, hier->GetChildUID(0));
		if (db->GetClass(eps.GetUID()) != kEPSItem)
			return UIDRef();
	}
	InterfacePtr<IPersistUIDData> data(eps, IID_IQRCODEDATAUID);
	return (data != nil && data->GetUID() != kInvalidUID && db->IsValidUID(data->GetUID())) ? eps : UIDRef();
}

// `text` as one line: each line break (CR, LF, a CR LF pair) one space, the ends trimmed.
PMString OneLine(const PMString& text)
{
	const WideString wide(text);
	WideString out;
	for (int32 i = 0; i < wide.CharCount(); ++i)
	{
		const UTF32TextChar c = wide.GetChar(i);
		const uint32 v = c.GetValue();
		if (v == kTextChar_LF && i > 0 && wide.GetChar(i - 1).GetValue() == kTextChar_CR)
			continue;		// the LF of a CR LF pair: its CR was the space
		if (v == kTextChar_CR || v == kTextChar_LF)
			out.Append(UTF32TextChar(kTextChar_Space));
		else
			out.Append(c);
	}
	PMString line(out);
	line.StripWhiteSpace(PMString::kLeadingAndTrailingWhiteSpace);
	line.SetTranslatable(kFalse);
	return line;
}

// "<a><between><b>", with only what is there: either side empty leaves the other alone, without the separator.
PMString Joined(const PMString& a, const char* between, const PMString& b)
{
	PMString out(a);
	if (!a.IsEmpty() && !b.IsEmpty())
		out.Append(between);
	out.Append(b);
	out.SetTranslatable(kFalse);
	return out;
}

}	// anonymous namespace

const char* KFCQRCodes::KindWord(Kind kind)
{
	switch (kind)
	{
		case kKindWeb:		return "Web";
		case kKindText:		return "Text";
		case kKindSMS:		return "SMS";
		case kKindEmail:	return "Email";
		case kKindVCard:	return "vCard";
		default:			return "";
	}
}

bool KFCQRCodes::Read(const UIDRef& frameOrItem, Kind& outKind, PMString& outContent)
{
	const UIDRef eps = QREpsOf(frameOrItem);
	if (eps.GetUID() == kInvalidUID)
		return false;
	InterfacePtr<IPersistUIDData> dataUID(eps, IID_IQRCODEDATAUID);
	const UIDRef data(eps.GetDataBase(), dataUID->GetUID());

	// THE KIND IS WHICH INTERFACE ANSWERS (no method returns IQRCodeStringData's kWebHyperlink .. kBusinessCard).
	InterfacePtr<IQRHyperLinkData> web(data, UseDefaultIID());
	if (web != nil)
	{
		outKind = kKindWeb;
		outContent = OneLine(web->GetLink());
		return true;
	}
	InterfacePtr<IQRPlainTextData> text(data, UseDefaultIID());
	if (text != nil)
	{
		outKind = kKindText;
		outContent = OneLine(text->GetText());
		return true;
	}
	InterfacePtr<IQRSMSData> sms(data, UseDefaultIID());
	if (sms != nil)
	{
		outKind = kKindSMS;
		outContent = Joined(OneLine(sms->GetPhoneNumber()), ": ", OneLine(sms->GetMessage()));
		return true;
	}
	InterfacePtr<IQREmailData> mail(data, UseDefaultIID());
	if (mail != nil)
	{
		outKind = kKindEmail;
		outContent = Joined(OneLine(mail->GetAddress()), "  ", OneLine(mail->GetSubject()));
		return true;
	}
	InterfacePtr<IQRvCardData> card(data, UseDefaultIID());
	if (card != nil)
	{
		// InDesign's own order for the name (the card's FN line: "<first> <last>" - KT 2026-10-27, M7).
		outKind = kKindVCard;
		outContent = Joined(Joined(OneLine(card->GetFirstName()), " ", OneLine(card->GetLastName())), " / ",
			OneLine(card->GetOrganization()));
		return true;
	}
	return false;
}

bool KFCQRCodes::CollectFrames(const UIDRef& docRef, std::vector<UID>& outFrames)
{
	outFrames.clear();
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return false;
	InterfacePtr<IObjectWalker> walker(::CreateObject2<IObjectWalker>(kObjectWalkerBoss));
	if (walker == nil)
		return false;
	// Every Include on and the Type All Frames - what InDesign's own object search walks with them all on - over this one
	// document.
	ObjectWalkerScopeOptions options(kTrue, kTrue, kTrue, kTrue, kTrue, ObjectWalkerScopeOptions::kAllFrames,
		ObjectWalkerScopeOptions::kDocument);
	options.SetDocumentToBeSearched(docRef);
	walker->Initialize(options);

	std::set<UID> seen;
	// (A guard on a walk that never ends - the object search keeps the same one, KFCObjectSearch's WalkDoc.)
	for (int32 step = 0; step < 100000; ++step)
	{
		UIDRef item;
		const IObjectWalker::WalkerState state = walker->GetNextItem(item);
		if (item.GetDataBase() == db && item.GetUID() != kInvalidUID)
		{
			const UIDRef eps = QREpsOf(item);
			if (eps.GetUID() != kInvalidUID)
			{
				// The FRAME is the row's item - the one the person selects. The walk hands back frames; an EPS item
				// handed back on its own is named by its parent.
				UID frame = item.GetUID();
				if (eps.GetUID() == item.GetUID())
				{
					InterfacePtr<IHierarchy> hier(eps, UseDefaultIID());
					frame = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
				}
				if (frame != kInvalidUID && seen.insert(frame).second)
					outFrames.push_back(frame);
			}
		}
		if (state == IObjectWalker::kWalkCompleted || state == IObjectWalker::kEndOfDocumentReached)
			break;
	}
	return true;
}

#ifdef KFC_DIAG
int32 KFCQRCodes::DiagProbe(PMString& outSummary)
{
	IDocument* const doc = KFCBookScope::ActiveDocument();
	if (doc == nil)
	{
		outSummary.Append("QR probe: no document.");
		return 0;
	}
	const UIDRef docRef = ::GetUIDRef(doc);
	std::vector<UID> frames;
	if (!CollectFrames(docRef, frames))
	{
		outSummary.Append("QR probe: no walker.");
		return 0;
	}
	int32 read = 0;
	for (size_t i = 0; i < frames.size(); ++i)
	{
		Kind kind = kKindNone;
		PMString content;
		if (!Read(UIDRef(docRef.GetDataBase(), frames[i]), kind, content))
			continue;
		++read;
		KFC_DIAG_LOG("QRPROBE %d %s %s", static_cast<int32>(frames[i].Get()), KindWord(kind), content.GetUTF8String().c_str());
	}
	outSummary.Append("QR probe: ");
	outSummary.AppendNumber(read);
	return 0;
}
#endif

// End, KFCQRCodes.cpp.
