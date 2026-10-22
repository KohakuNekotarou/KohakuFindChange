//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCQueryContents.h. UI side: the dialog that says it is the UI half's, and the file is only read.
//
//  THE FILE'S SHAPE (InDesign's own queries - Presets\Find-Change Queries - and the user's, which the Find/Change
//  dialog saves): <Query><Header> ... <QueryType value="Text"/> ... </Header><Description> ... </Description></Query>,
//  every value in a "value" attribute. Under Description: the text to find and to change to - FindString and
//  ReplaceString in a Text query, FindExpression and ReplaceExpression in a GREP one - and FindFormatSettings and
//  ReplaceFormatSettings, whose children are the formats set (none = no format). An Object query has no text; no Glyph
//  or Transliterate query was there to read on this machine, so of those only the formats are said.
//
//  THE PARSER IS INDESIGN'S, in the SDK's shape for reading an XML file (chlinguistic's CHLUserDictUtils.cpp,
//  xmlcataloghandler's XCatHndFacade.cpp - and KCM's KCMResourceParse.cpp, which is ours): the XML parser service's
//  ISAXServices, a content handler boss of this plug-in's (kKFCQueryContentsHandlerBoss), ParseStream. What they
//  taught:
//    * ParseStream answers TRUE WHEN IT FAILED - "Returns 0 if it succeeded" (XCatHndFacade.cpp), the header says
//      nothing of it;
//    * the handler handed to ParseStream is the DEFAULT one: with no element name registered every element reaches
//      it, and HandlesSubElements() kTrue keeps the children coming;
//    * Register is called by hand, as the samples do;
//    * the parser's warning alert is turned off - picking a row must never put a dialog up;
//    * a parse that fails leaves the error state raised, and the next command would fail on it (the chlinguistic
//      sample clears it): cleared here, and the caller's own state put back (GlobalErrorStatePreserver);
//    * nothing may throw out of the handler and through the parser (KCM's handler catches around what can grow).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "IPMStream.h"
#include "ISAXAttributes.h"
#include "ISAXContentHandler.h"
#include "ISAXParserOptions.h"
#include "ISAXServices.h"
#include "ISession.h"				// GetExecutionContextSession - where the service registry lives

// General includes:
#include "CreateObject.h"			// ::CreateObject2 - the handler boss
#include "CSAXContentHandler.h"		// the SDK's base for a content handler - an empty body for every method
#include "ErrorUtils.h"				// GlobalErrorStatePreserver / PMSetGlobalErrorCode - a failed parse's error
#include "FileUtils.h"				// SysFileToPMString - the tooltip's file
#include "StreamUtil.h"				// CreateFileStreamRead
#include "WideString.h"
#include "XMLParserID.h"			// kXMLParserService / kXMLParserServiceBoss

// Project includes:
#include "KFCQueryContents.h"
#include "KFCQueryOrder.h"			// KindAndName - "<kind>  <name>", as the rows read
#include "KFCTextCut.h"				// KFCSafeCut - where a shown text may be cut
#include "KFCUIID.h"

/** What a query's file says, as far as the dialog tells it. */
struct KFCQueryFacts
{
	bool		hasFind;			// the query has a text to find (Text, GREP) - empty is a text too
	bool		hasChange;			// ...and to change to
	PMString	find;
	PMString	change;
	bool		findFormat;			// FindFormatSettings has a child
	bool		changeFormat;		// ReplaceFormatSettings has a child

	KFCQueryFacts() : hasFind(false), hasChange(false), findFormat(false), changeFormat(false) {}
};

/** The content handler InDesign's parser calls as it reads a query's file (kKFCQueryContentsHandlerBoss). It only
    notes what it meets into the KFCQueryFacts the reader gives it for one parse (SetFacts). */
class KFCQueryContentsHandler : public CSAXContentHandler
{
public:
	KFCQueryContentsHandler(IPMUnknown* boss) : CSAXContentHandler(boss), fFacts(nil), fIn(kNowhere) {}
	virtual ~KFCQueryContentsHandler() {}

	// kTrue: no element name is registered, so this is the default handler, and kTrue keeps every element's children
	// coming to it too - the formats are the children of FindFormatSettings and ReplaceFormatSettings.
	virtual bool16 HandlesSubElements() const { return kTrue; }
	virtual void StartElement(const WideString& uri, const WideString& localname, const WideString& qname,
		ISAXAttributes* attrs);
	virtual void EndElement(const WideString& uri, const WideString& localname, const WideString& qname);

	/** Where this parse's facts go - nil after it, so the handler never holds the reader's object past it. */
	void SetFacts(KFCQueryFacts* facts)
	{
		fFacts = facts;
		fIn = kNowhere;
	}

private:
	enum Settings { kNowhere, kFindFormat, kChangeFormat };

	KFCQueryFacts*	fFacts;
	Settings		fIn;		// inside FindFormatSettings or ReplaceFormatSettings - a child there is a format set
};

CREATE_PMINTERFACE(KFCQueryContentsHandler, kKFCQueryContentsHandlerImpl)

void KFCQueryContentsHandler::StartElement(const WideString& /*uri*/, const WideString& localname,
	const WideString& /*qname*/, ISAXAttributes* attrs)
{
	if (fFacts == nil)
		return;
	try
	{
		if (fIn == kFindFormat)
		{
			fFacts->findFormat = true;
			return;
		}
		if (fIn == kChangeFormat)
		{
			fFacts->changeFormat = true;
			return;
		}
		if (localname == WideString("FindFormatSettings"))
		{
			fIn = kFindFormat;
			return;
		}
		if (localname == WideString("ReplaceFormatSettings"))
		{
			fIn = kChangeFormat;
			return;
		}
		const bool find = (localname == WideString("FindString") || localname == WideString("FindExpression"));
		const bool change = (localname == WideString("ReplaceString") || localname == WideString("ReplaceExpression"));
		if ((!find && !change) || attrs == nil)
			return;
		WideString value;
		if (!attrs->GetValue(WideString("value"), value))
			return;
		PMString text(value);
		text.SetTranslatable(kFalse);
		if (find)
		{
			fFacts->hasFind = true;
			fFacts->find = text;
		}
		else
		{
			fFacts->hasChange = true;
			fFacts->change = text;
		}
	}
	catch (...)
	{
		// Nothing goes out through the parser: what this element held is simply not said.
	}
}

void KFCQueryContentsHandler::EndElement(const WideString& /*uri*/, const WideString& localname,
	const WideString& /*qname*/)
{
	if (localname == WideString("FindFormatSettings") || localname == WideString("ReplaceFormatSettings"))
		fIn = kNowhere;
}

namespace
{
	/** The longest text a tooltip shows of one part - InDesign's own tips stop at a thousand characters
	    (conditionaltextui's ConditionalTextTips.cpp, kMaxTipsChars), and this one carries two texts and a path. */
	const int32 kTipTextChars = 400;

	/** Read the query's file into `out`. false = it could not be opened, or did not read as XML. */
	bool ReadQueryFacts(const IDFile& file, KFCQueryFacts& out)
	{
		out = KFCQueryFacts();
		// The caller's error state comes back as it was; what the parse raises is cleared before it is put back.
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamRead(file));
		if (stream == nil)
			return false;
		ISession* const session = GetExecutionContextSession();
		InterfacePtr<IK2ServiceRegistry> registry(session, UseDefaultIID());
		if (registry == nil)
			return false;
		InterfacePtr<IK2ServiceProvider> xmlProvider(
			registry->QueryServiceProviderByClassID(kXMLParserService, kXMLParserServiceBoss));
		InterfacePtr<ISAXServices> saxServices(xmlProvider, UseDefaultIID());
		InterfacePtr<ISAXContentHandler> handler(::CreateObject2<ISAXContentHandler>(kKFCQueryContentsHandlerBoss));
		if (saxServices == nil || handler == nil)
			return false;

		// The boss's one ISAXContentHandler is this file's (KFCUI.fr), reached as its own class the way the
		// chlinguistic sample reaches its handler's word list.
		KFCQueryContentsHandler* const ours = static_cast<KFCQueryContentsHandler*>(handler.get());
		ours->SetFacts(&out);
		handler->Register(saxServices);
		InterfacePtr<ISAXParserOptions> options(saxServices, UseDefaultIID());
		if (options != nil)
			options->SetShowWarningAlert(kFalse);
		const bool16 failed = saxServices->ParseStream(stream, handler);
		ours->SetFacts(nil);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return failed == kFalse;
	}

	/** `"<text>"`, cut at `limit` characters (0 = not cut) where a shown text may be cut, "..." after a cut. */
	void AppendQuoted(PMString& out, const PMString& text, int32 limit)
	{
		PMString shown(text);
		shown.SetTranslatable(kFalse);
		if (limit > 0 && shown.CharCount() > limit)
		{
			shown.Truncate(shown.CharCount() - KFCSafeCut(shown, limit));	// Truncate takes off that many from the end
			shown.Append("...");
		}
		out.Append("\"");
		out.Append(shown);
		out.Append("\"");
	}

	/** The query's head, "<kind>  <name>", and false with the line finished when its file is not to be read. */
	bool Head(const KFCSavedQuery& query, KFCQueryFacts& facts, PMString& out)
	{
		out = KFCQueryOrder::KindAndName(query);
		out.SetTranslatable(kFalse);
		// Asked of the disk now, not of the row's mark - that was read when the dialog opened or loaded the order, and
		// a query can be deleted (or put back) since: the double click and Find ask the same way (case qd-pick-gone).
		if (!FileUtils::DoesFileExist(query.file))
		{
			out.Append(": its file is not there.");
			return false;
		}
		if (!ReadQueryFacts(query.file, facts))
		{
			out.Append(": its file could not be read.");
			return false;
		}
		return true;
	}
}

PMString KFCQueryContentsLine(const KFCSavedQuery& query)
{
	KFCQueryFacts facts;
	PMString line;
	if (!Head(query, facts, line))
		return line;
	PMString parts;
	parts.SetTranslatable(kFalse);
	if (facts.hasFind)
	{
		parts.Append("  Find what: ");
		AppendQuoted(parts, facts.find, 0);			// the message line cuts what does not fit
	}
	if (facts.hasChange)
	{
		parts.Append("  Change to: ");
		AppendQuoted(parts, facts.change, 0);
	}
	if (facts.findFormat)
		parts.Append("  + Find Format");
	if (facts.changeFormat)
		parts.Append("  + Change Format");
	line.Append(":");
	line.Append(parts.IsEmpty() ? PMString("  (no text or format to show)") : parts);
	return line;
}

PMString KFCQueryContentsTip(const KFCSavedQuery& query)
{
	KFCQueryFacts facts;
	PMString tip;
	if (!Head(query, facts, tip))
		return tip;
	if (facts.hasFind)
	{
		tip.Append("\nFind what: ");
		AppendQuoted(tip, facts.find, kTipTextChars);
	}
	if (facts.hasChange)
	{
		tip.Append("\nChange to: ");
		AppendQuoted(tip, facts.change, kTipTextChars);
	}
	tip.Append(facts.findFormat ? "\nFind Format: set" : "\nFind Format: none");
	tip.Append(facts.changeFormat ? "\nChange Format: set" : "\nChange Format: none");
	tip.Append("\n");
	tip.Append(FileUtils::SysFileToPMString(query.file));
	tip.SetTranslatable(kFalse);
	return tip;
}

// End, KFCQueryContents.cpp.
