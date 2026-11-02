//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT A RUBY OR KENTEN SEARCH TAKES IN (the author, 11-02: the icons InDesign's Text tab has under its Search:, on the Kohaku+ tab - KFCOwnSwitches): these kinds walk the stories themselves, so nothing of InDesign's walker scope asks it for them. THE SAME ANSWER AS INDESIGN'S TAB,
//  measured place by place (work/note-scripts/2026-11-02-kfc-ruby-list/measure_switches2.jsx, measure_switches3.jsx - doc.findText() under each switch):
//    a story's lock: Include Locked Stories;
//    A FOOTNOTE'S TEXT: Include Footnotes - and then, as all text is, by its frame. (An endnote is no footnote: it is text in a frame of its own.)
//    BY THE FRAME IT IS COMPOSED INTO: Include Master Pages for a parent's; Include Hidden Layers and Hidden Objects for a frame on a hidden layer or hidden itself, in a hidden group, or anchored in a frame that is; Include Locked Layers and Locked Objects for the same of a lock.
//    TEXT COMPOSED INTO NO FRAME (overset - a footnote that does not fit too): found, whatever its story's frames are.
//  Locked layers and stories are Search Only: such a row is listed, marked locked by BuildHit as any other is, and its Replace is refused.
//
//========================================================================================

#ifndef __KFCOwnScope_h__
#define __KFCOwnScope_h__

#include "BaseType.h"
#include "UIDRef.h"

#include <map>
#include <string>

#include "KFCFold.h"
#include "KFCModelTypes.h"		// KFCOwnSwitches

class ITextModel;

class KFCOwnScope
{
public:
	/** For one document's walk: a frame's answers are read once. */
	explicit KFCOwnScope(const KFCOwnSwitches& switches) : fSwitches(switches) {}

	/** Is the story in at all? (Its own lock - a question of the story, asked before its text is read.) */
	bool TakesStory(const UIDRef& story) const;

	/** Is the text at `at` in? */
	bool Takes(const UIDRef& story, ITextModel* model, TextIndex at);

private:
	struct FrameAnswers
	{
		bool	onMaster;
		bool	hidden;
		bool	locked;
	};

	KFCOwnSwitches				fSwitches;
	std::map<UID, FrameAnswers>	fFrames;
};

/** What the two switches say of comparing (KFCFold.h). */
inline KFCFold::Flags KFCFoldFlagsOf(const KFCOwnSwitches& switches)
{
	return KFCFold::Flags(switches.kanaSensitive, switches.widthSensitive);
}

#ifdef KFC_DIAG
/** TEST BUILDS - a line of the fault switches ruby-tab and kenten-tab: on=<icon> or off=<icon>, the icons lockedlayers, lockedstories, hidden, master, footnotes, kana, width (the model's cases, which need no dialog). */
inline void KFCOwnScopeDiagLine(const std::string& line, KFCOwnSwitches& io)
{
	const bool on = (line.compare(0, 3, "on=") == 0);
	const bool off = (line.compare(0, 4, "off=") == 0);
	if (!on && !off)
		return;
	const std::string icon(line.substr(on ? 3 : 4));
	if (icon == "lockedlayers")
		io.lockedLayers = on;
	else if (icon == "lockedstories")
		io.lockedStories = on;
	else if (icon == "hidden")
		io.hiddenLayers = on;
	else if (icon == "master")
		io.masterPages = on;
	else if (icon == "footnotes")
		io.footnotes = on;
	else if (icon == "kana")
		io.kanaSensitive = on;
	else if (icon == "width")
		io.widthSensitive = on;
}
#endif

#endif // __KFCOwnScope_h__

// End, KFCOwnScope.h.
