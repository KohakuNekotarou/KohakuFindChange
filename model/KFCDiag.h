//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  TEST-BUILD TRACING, BOTH HALVES (the author's call: "keep it, behind a build flag"). One line per
//  event to %TEMP%\kbs-diag.txt, each led by the time in milliseconds.
//
//  OFF unless the build defines KFC_DIAG:
//      msbuild build\win\prj\KohakuFindChange.vcxproj /p:Configuration=Release /p:Platform=x64
//              /p:KFCExtraDefines=KFC_DIAG
//  (both projects - KohakuFindChange and KohakuFindChangeUI - pass $(KFCExtraDefines) to the compiler;
//  work\kbs-regress\cycle-build.ps1 -Diag does it).
//  In a build without it every KFC_DIAG_LOG compiles to nothing - its arguments are not evaluated either -
//  so the shipping .pln holds no call, no format string and no file name. Anything a trace needs that the
//  product does not (a helper that walks state to print it) goes inside #ifdef KFC_DIAG with it.
//
//  Why it exists: the panel's following of Undo (KFCUndoFollow) depends on notifications InDesign delivers
//  at idle, and when one does not come there is nothing on screen to say so. A trace of what arrived and
//  what was decided found a lost observer (a watched story purged from memory) in one run, after eighteen
//  runs of reading the panel alone could not.
//
//  AND FAULT SWITCHES. KFC_DIAG_FAULT("name") is true while
//  the file %TEMP%\kbs-diag-fault-<name> exists - a test creates it and deletes it again - so a test build can
//  reach a state the product only gets into after something ELSE has failed (a window that would not open).
//  Asked at each call, so the switch takes effect at once. In a build without KFC_DIAG it is the constant false, and
//  the macro drops its argument with it - so no name of a switch reaches a shipping .pln, inside #ifdef KFC_DIAG or not
//  (most uses sit inside one; KFCObjectReplace.cpp's object-walk-miss does not need to).
//    keep-held   KFCBookScope::HandBackHeldDocNow keeps a held chapter instead of closing it
//                (work\kbs-regress\cases\fault-keep-held-on.jsx / -off.jsx)
//    fcmin-decoy (UI half) the file holds a window handle in hex; ui/KFCFindChangeMinimize.cpp reads it
//                itself and points its record at that window before putting the dialog's style back - a
//                handle the OS has since given to another window (work\kbs-fcmin\decoy.ps1 makes one)
//    jump-no-front (UI half) ui/KFCJump.cpp's EnsureDocFrontmost reports that the hit's window
//                could not be brought forward (work\kbs-jump\j2-select-unfronted.ps1; work\kbs-regress\hid-cases.tsv - a
//                chapter a landing reopened goes back when its window does not come forward)
//    replace-refuse KFCReplaceEngine.cpp's WalkStoryReplacing takes InDesign's replace command as having
//                refused every row - a chapter where nothing lands (work\kbs-regress\cases\fault-replace-refuse-on.jsx
//                / -off.jsx)
//    queries-run  the file holds one query path per line (UTF-8); the panel's Find runs the query run over them
//                (KFCQuerySequence::RunFromDiagSwitch) - the way in without the query dialog
//                (work\kbs-regress\cases\qs-*.jsx write it, qs-off.jsx takes it off)
//    perf-fresh-walker  the file holds "<mode> <n>": WalkStoryReplacing starts its walk again after every n writes -
//                mode 2 from where the last write ended, mode 3 from the top of the story - with a new walker, client and
//                scope (docs/ai-notes/kfc-speedup-ideas-2026-10-05.md: is the find's slowing down kept in the
//                walk?). Throwaway documents only.
//    perf-commands  every command InDesign processes is counted, by class, over a search and over a query run
//                (KFCDiagCommands.h - "COMMANDS" lines). The count itself costs time: not on a timing run.
//    perf-backward  KFCReplaceEngine.cpp's WriteBackward answers yes: every write walks backward (the same study:
//                what the direction does to the find's time). Throwaway documents only.
//    tree-expand  (UI half) the file holds "<mode>": KFCResultTree::Rebuild opens the rows another way - mode 1 one
//                ExpandNode with all its descendants per open document row, mode 2 every row opened before ChangeRoot
//                (the same note: what opening the story rows one at a time costs). Throwaway documents only.
//    changeall-cancel  KFCChangeAll::Run takes Cancel as pressed after its first chapter - every chapter must come
//                back as it was (case all-cancel-book, ca-cases.tsv). Throwaway documents only.
//    run-list-cap  the file holds "<n>": the list a query run leaves holds n rows (KFCRunList - case rl-cap, rl-cases.tsv).
//    run-list-mismatch  KFCRunList takes the find of the run's first document as having selected one more than its Change All
//                wrote - that document is left out of the list and said (case rl-mismatch).
//    no-window   KFCBookScope::ShowChapterWindow answers that the window could not be opened - a written chapter stays
//                held and windowless; Change All in Book and the query run must say "left open with no window"
//                (work\kbs-regress\xq-cases.tsv). Throwaway documents only.
//    replace-walk-fail  KFCReplaceEngine.cpp's WalkStoryReplacing takes InDesign's find as having stopped with an error
//                before the first match - the row's Replace must say so, not "not found where the search found it"
//                (work\kbs-regress\xq-cases.tsv). Throwaway documents only.
//    queries-cancel  the file holds "<n>": KFCQuerySequence.cpp's Run takes Cancel as pressed while its step n
//                (1-based, a query x document) was being written - heard where the run asks the bar next, so a test can
//                press it in the run's last step, which only the ask after the loop hears
//                (work\kbs-regress\xq-cases.tsv). Throwaway documents only.
//    book-selection  (UI half) the file's first line "book=<a book's file name>", then one chapter's short name a line:
//                ui/KFCBookPanelLookup.cpp's GetPanelBookSelection answers with those chapters as the Book panel's
//                selection for that book (nothing selected for any other), judged by the same rule as a real one -
//                Find/Change Selected Documents (Book) tested apart from the panel (work\kbs-regress\sd-cases.tsv; the
//                make scripts' selectChapters writes it, qd-off.jsx takes it off)
//    no-return-filter  (UI half) ui/KFCResultTreeEH.cpp's PushReturnFilter pushes nothing: the result list's
//                Return filter stays off the dispatcher's stack - what the filter alone changes, told apart from the rest
//                (a jumped-to replaced row drawn hilited)
//    menu-drop-grey  (UI half) ui/KFCResultNodeEH.cpp's PopRowMenu passes HandlePopupMenu removeDisabledItems kTrue, the
//                old way: a row menu drops its grey items, and one with nothing else does not open (SET-11 - the grey
//                items stay)
//    click-acquire-only  (UI half) ui/KFCResultNodeEH.cpp's row click hands the keyboard to the list the old way
//                - IKeyBoard alone, and only when it names another holder - not through the panel's doors
//                (KFCResultTree::TakeKeyboard): the case that shows what the doors add after Find/Change's title bar was
//                clicked (work\kbs-regress\archive\cases\fault-click-acquire-only-on.jsx)
//    objsearch-cancel  the file holds "<n>": the Object tab's search (KFCObjectSearch.cpp's WalkDoc) takes Cancel as pressed
//                once a document's walk has listed its nth item - heard inside the document, where the bar is asked
//                (work\kbs-regress\ms-cases.tsv, cases\fault-objsearch-cancel-on.jsx / -off.jsx)
//    object-walk-miss  an object row's Replace (KFCObjectReplace.cpp) takes InDesign's walk as never coming to the row's
//                item - the row must say it no longer matches, nothing written (work\kbs-regress\oca-cases.tsv, oca-walk-miss)
//    object-prints-stale  an object write (KFCObjectReplace.cpp's FollowRowsTheWriteMoved) leaves the other rows' fingerprints
//                as they were, the old way: the text frame an inline written hangs in is then refused as "changed since the
//                search" (obj-f2-inline-armed)
//    object-prints-unbacked  KFCResultModel::SetHitItemPrint sets a row's print with no copy aside: an Undo of the write
//                leaves the row at the write's print while the item goes back, and the row is refused
//                (obj-f2-undo-frame-armed)
//    object-prints-blind  KFCObjectReplace.cpp's RowsTheWriteCanMove takes every row the write can reach, without asking
//                whether it read as its row held it before the write: an edit of someone else's is then given the write's
//                print, and the row's Replace writes over it (obj-f2-edited-refused-armed)
//    return-repeat  (UI half) ui/KFCResultTreeEH.cpp's TakeReturn lets a held Return's repeats through, the old way
//                - each one a write: the case that shows what taking the repeat stops (ret-shift-held-armed)
//    return-filter-holder-only  (UI half) ui/KFCResultTreeEH.cpp's Return filter asks InDesign's keyboard holder alone,
//                the old way - not the window the system sends the keys to as well: the case that shows what the second
//                question stops, a Return pressed after Find/Change's title bar was clicked (ret-after-fc-title-armed)
//    object-rows-sweep  (UI half) ui/KFCJump.cpp's ReplaceObjectRows fronts the shown row's chapter WITH the
//                "Hide Previous Chapter" sweep. The sweep is decided at the end of the operation (the spec map's
//                JMP-26c), after the write, and keeps the written chapters (sel-book-objects-open-hide-deferred); with
//                sweep-at-once as well it is scheduled in the middle, the old way - the other chapters it is about to
//                write are closed after the write, the write in them: the case that shows the written-document check
//                (KFCDiagWrittenDocs.h) catching it (sel-book-objects-open-hide-armed)
//    sweep-at-once  (UI half) ui/KFCJump.cpp's FrontChapter schedules the "Hide Previous Chapter" sweep on the spot, in the
//                middle of the operation, instead of leaving it to the operation's SweepAtEnd - the old way
//                (sel-book-objects-open-hide-armed, with object-rows-sweep)
//    clean-after-write  KFCReplaceEngine.cpp's row Replace takes each written document's "unsaved" flag down after the
//                write - a close would then throw the write away unasked: the case that shows the written-document check
//                catching it (written-cleaned-armed)
//    queries-error-stands  KFCQuerySequence.cpp's Run raises the global error code after its last step, reporting
//                nothing - the run must take itself back and say "Stopped", and process no command over the error on
//                its way out (ClearFindChange - a command processed over it is InDesign's protective shutdown)
//                (xq-error-stands)
//    landing-return  (UI half) ui/KFCJump.cpp's JumpToHit replaces its own row through the panel's door
//                (KFCResultTree::ReplaceRows) once its window is in front - the Return a landing's message loop can
//                dispatch: the door must turn it away (landing-return)
//    landing-guard-off  (UI half) KFCJump::IsLanding answers no, the old way: with landing-return the write goes in
//                inside the landing's dirty guard, is marked clean when the landing ends, and the next landing's
//                Hide Previous Chapter sweep closes it unsaved - the case that shows the written-document check
//                catching it (landing-return-armed)
//    select-all-expand-once  (UI half) Select All Rows opens the rows under its row with one ITreeViewMgr::ExpandNode with
//                all its descendants, the old way (ui/KFCResultTreeController.cpp): on a book result as the search left it -
//                the book row open, its chapters closed - the rows under them would be selected and not drawn, and Return,
//                which asks the selection in the order drawn, would write none (sel-book-closed-return-armed)
//    list-landing-guard-off  (UI half) the result list's keys (the arrows, Home / End, PageUp / PageDown) ask only whether a
//                walk of theirs is still landing, and a row's press asks nothing - the old way (ui/KFCResultTreeEH.cpp's
//                MoveUnderWay, ui/KFCResultTreeController.cpp's ProcessSelectionRules): a key or a press that a click's
//                landing dispatches moves the selection while its
//                own landing is turned away - the list on one row, the page on another (land-arrow-closed-armed,
//                land-click-closed-armed)
//    home-end-stock  (UI half) the result list's Home / End / PageUp / PageDown move the selection alone, the old way
//                (ui/KFCResultTreeEH.cpp's StockMoveThenLand): the list on the row they reach, the page left on the row before
//                (click-end-lands-armed, click-home-lands-armed)
//    scope-walk-off  KFCSearchEngine::ScopeHasNothingToWalk answers no, the old way: a document with no story to walk (no
//                text frame, or master-page text only with Include Master Pages off) meets InDesign's kFailure again - the
//                search says it "stopped with a search error" and Change All in Book / a query run stops whole
//                (sc-alldocs-notext-armed, ca-book-notext-chapters-armed)
//    qd-order-reset  (UI half) the query dialog starts from an empty run order and takes the switch off again - one
//                case's order not carried into the next (ui/KFCQueryDialog.cpp; the qd cases)
//    qd-order-file  (UI half) the file's first line is the file Save Order... and Load Order... use, in place of
//                Windows' own dialogs, which a test cannot press (ui/KFCQueryOrderFile.cpp; KFCDiagFaultText below)
//    boundary-mismatch  (both halves) each half reads the other's boundary stamp as another build's: the UI half stands
//                in for the model's doors - every command grey, the message line saying why (ui/KFCModelAccess.cpp) -
//                and the model half takes the UI half for absent (KFCQueryUIServices, IKFCUIServices.h). The UI half
//                reads it once, at its first door: set it before InDesign starts (the steps: KN-3 in
//                docs/ai-notes/new-knowledge-sweep-kfc-b0-b5-2026-10-13.md)
//    ordinal-runs  KFCResultModel.cpp's NumberHitsWithinPages takes a run of one pageIndex for one page, the old way: the
//                rows of two parent pages (and the pasteboard's) are numbered as one page's, and a parent page's rows with
//                another place's between them are not numbered (row-ordinal-pageless-armed)
//    shown-empty-raw  KFCResultModel.cpp's ReplaceStoryRows counts a row taken off the list (Clear from Results) as a
//                row, the old way: a document whose other stories' rows were all taken off loses its row when its last
//                story is searched again with nothing found (dr-story-zero-keeps-row-cleared-armed)
//    look-again-list-limit  KFCSearchEngine.cpp's CollectStoryHits walks a story only as far as the list's limit (300
//                matches) and takes a walk cut there for a whole one, the old way: a row whose match stands past its story's
//                300th is called missing when it is looked for again (look-again-past-limit-armed)
//    inline-layer-off  KFCPageItemFacts.cpp's QueryDocLayerOf reads an item placed in text (an inline or anchored frame) as on
//                no layer, the old way: a hit in an anchored frame whose anchor stands on a locked or hidden layer is listed as
//                neither (layer-anchored-locked-armed, layer-anchored-hidden-armed, obj-anchored-locked-armed)
//    frame-read-uncomposed  KFCSearchEngine.cpp's FrameUIDForPosition reads the parcels as they stand, the old way
//                (ComposeThrough does nothing). With own-walker as well, the whole of the old way: a match pushed
//                into overset by an edit not yet composed is listed in the frame it stood in before the edit
//                (overset-after-edit-armed); alone, what the shared walker's find composes by itself
//                (overset-after-edit-shared-only)
//    own-walker  KFCSearchEngine::AcquireWalker hands back a kBasicTextWalkerBoss of KFC's own instead of the session's
//                shared walker - the way before the author's call for the shared one (the speed-up's S1): its find
//                composes nothing, and the dialog's Find Next is left where it was (overset-after-edit-armed, with
//                frame-read-uncomposed)
//    run-dirty-unwritten  KFCChangeAll::WriteDocument leaves a document it changed nothing in marked modified, as a walk
//                can - Change All in Book and the query run must make it clean again and hand it back
//                (xq-book-dirty-unwritten, xq-doc-dirty-unwritten, ca-book-dirty-unwritten). Throwaway documents only.
//    run-leave-no-flag  KFCChangeAll::LeaveRunDocuments leaves a document not written as the walk left it, the query
//                run's old way: with run-dirty-unwritten, a chapter not written stays open with no window and named, and a
//                document of the user's stays "unsaved" (xq-book-dirty-unwritten-armed, xq-doc-dirty-unwritten-armed)
//    query-scope-refuse  KFCQueryScope (a row operation under its list's query - the tier switch v2) refuses once every
//                check has passed: the refusal's whole path - the sequence aborted, Find/Change put back, nothing done -
//                tried on demand (the v2 spec's section 5, item 7). One-shot.
//    putback-over-error  KFCQueryScope::Finish leaves an error standing for its put-back of Find/Change to meet - what
//                an operation that failed without clearing would leave - and clears it after: the QSCOPE line says
//                whether Find/Change was put back over it and whether the error stood again (hd-sw-case-putback-error).
//                One-shot.
//    copy-fail  KFCCopyStore::Take keeps no copy: a row's Replace goes through all the same, and that row's
//                Restore from Copy says no copy could be kept
//                (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md C3, R16)
//    copy-links  TakeRowCopy's cross-reference door stands open: a row whose copy touches a cross-reference source keeps
//                its copy all the same (its hyperlink sources are kept and put back as one link - the same spec's C15)
//    restore-link-in-way  Restore from Copy pastes over a hyperlink source whose whole text lies on the paste, the old
//                way: a kept link the Replace left on what it wrote is not moved out of the paste's way, and one made
//                there since does not leave the row - the paste leaves the source with no text (KFCReplaceEngine.cpp,
//                MoveLinksOutOfPaste - tier-cases.tsv, restore-link-inside-named-armed, restore-link-made-after-armed)
//    qcopy-roundtrip / qcopy-armed / qcopy-abort  the panel's Find copies Find/Change's query in memory, states it
//                back and says what came back, searching nothing (KFCQueryCopier::DiagProbe - KFCQueryCopy.h says what
//                each does; work\note-scripts\2026-10-14-kfc-tier-switch\spike.ps1). One-shot.
//    object-limit  the file holds a number: KFCObjectSearch::CollectTargets takes it for the object lists' limit of
//                rows in place of kKFCCollectObjectLimit - the limit measured before it was chosen (M6 of
//                docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md -
//                work\note-scripts\2026-10-18-kfc-narrow\m6.ps1)
//    bg-first-view  (UI half) ui/KFCUIServices.cpp's ForEachWindowSelection hands back each document's first layout
//                view alone (ILayoutViewUtils::GetAllLayoutViews), the old way: an object search's walk leaves its last
//                item selected in a minimised window and in a document's second window (obj-bg-sel-minimised-armed,
//                obj-bg-sel-second-window-armed)
//    again-no-repaint  (UI half) a Search ... Again or a Narrow Down turned away or cancelled leaves the rows as drawn,
//                the old way (ui/KFCResultListWidgetMgr.cpp's SearchAgain, ui/KFCResultHistory.cpp's Narrow): the rows
//                its walk had follow their foci first - a line read again - stay drawn as before
//                (sr-toend-edge-repaint-armed)
//    copy-name-from-label  (UI half) Copy Results' Document column reads a document row's name off its label, the old
//                way (ui/KFCCopyResults.cpp's DocumentName): a document with no window, searched with All Documents, is
//                named "<name> (no window)" (copy-results-nowindow-armed)
//    frame-owner-climb  Frames of These Matches climbs a match's frame's hierarchy to the first item with
//                IGraphicFrameData, the old way (KFCNarrow::FrameOwner): text on a path reaches its
//                kTOPSplineItemBoss - which carries IGraphicFrameData - and not the path (nd-frames-path-armed)
//    narrow-ceiling-first  a narrowed list's text walk (KFCSearchEngine.cpp's CollectHitsInDoc) asks the list's ceiling
//                ahead of its conditions, of every match it meets, the old way: a full list met by one more match the
//                conditions drop is called cut short - "Stopped at the 300 limit" (nd-limit-dropped-armed)
//    part-narrow-as-text  a Narrow Down over part of a document (KFCSearchEngine.cpp's SearchPartsAgain) files a
//                Frames of These Matches list with a text list's limit and words, and says nothing of the matches no
//                frame holds, the old way (nd-frames-part-armed)
//    ellipsize-half  (UI half) ui/KFCPanelTextDraw.h's KFCEllipsize takes every width half a pixel wider: a UI at 100 %
//                hands only whole widths, and PMEllipsizeString's band (where it never comes back) lies between two -
//                the way into it for the guard's own test
//                (work\note-scripts\2026-10-25-kfc-full-run\green_ellipsize.ps1)
//    qr-probe  KFCSearchEngine.cpp's RunSearch only reads the active document's QR codes into the trace - a QRPROBE
//                line each: the frame's UID, the kind, the content (KFCQRCodes::DiagProbe; qr-probe-kinds)
//    qr-tab  KFCQRSearch::IsTabCurrent takes KFC's QR Code tab for Find/Change's tab whatever the dialog shows - the panel's
//                Find lists QR codes; the file's first line "all" = its Search: All Documents, anything else Document (the
//                model's cases, which need no dialog - work\kbs-regress\qr-cases.tsv, fault-qr-tab-*.jsx)
//    table-tab  KFCTableSearch::IsTabCurrent takes KFC's Table tab for Find/Change's tab whatever the dialog shows; the file's lines say
//                what it asks - find=<style>, overrides=1, change=<style>, clear=1, scope=all (the model's cases, which need no dialog -
//                work\kbs-regress\tb-cases.tsv)
//    table-cap  KFCTableSearch::CollectTargets lists at most 2 tables (tb-cap)
//    ruby-tab  KFCRubySearch::IsTabCurrent the same for KFC's Ruby tab; the file's lines kind=without, base=, ruby=, change=, remove=1, scope=all (work\kbs-regress\rb-cases.tsv)
//    qr-scope-unkept  (UI half) a landing with KFC's QR Code tab shown - a QR code Find Next's, a row's click
//                (ui/KFCJump.cpp's ArmNativeScopeKeep) - keeps no Search: of InDesign's own tab: what the open dialog does
//                to it when the landing selects (qr-findnext-native-scope-armed). Measured: it does nothing while
//                the QR Code face is shown, so that case does not arm (work\kbs-regress\unarmed-qr-cases.tsv)
//
//  AND TIMERS: KFC_CLOCK / KFC_SPENT add up the milliseconds a stretch of code takes, and KFCDiagPhase
//  writes "PHASE <name> begin" and "PHASE <name> end <ms>" around a scope - all of it nothing without KFC_DIAG.
//
//========================================================================================

#ifndef __KFCDiag_h__
#define __KFCDiag_h__

#ifdef KFC_DIAG

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <io.h>							// _access_s (KFCDiagFault)
#include <chrono>
#include <mutex>
#include <string>						// KFCDiagFaultBytes, Text and Lines
#include <thread>
#include <vector>						// KFCDiagFaultLines
#include "PerformanceStats.h"			// InDesign's own counters (KFCDiagPerf) - guide vol2-16
#include "PerformanceMetricsID.h"

// Appends one line: "<ms since the epoch> <the formatted text>". Opened and closed per line, so a crash
// right after it still leaves the line on disk.
//
// ONE WRITER AT A TIME (guide vol1-07: a file a plug-in writes is synchronised like a global). The model
// half's code is called on a background task's thread too (an export's - KFCUndoFollow writes its line before its own
// main-thread gate), and fopen_s opens for this caller alone: a second append while the file is open fails, and its
// line was lost without a word. The lock puts this half's threads in a queue; the other half (the UI plug-in, main
// thread only) can still meet a background write, so a refused open is tried again a few times before the line goes.
inline void KFCDiagLog(const char* fmt, ...)
{
	static std::mutex writer;
	const std::lock_guard<std::mutex> lock(writer);
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag.txt", temp);
	free(temp);
	FILE* f = nullptr;
	for (int attempt = 0; attempt < 20; ++attempt)
	{
		if (fopen_s(&f, path, "a") == 0 && f != nullptr)
			break;
		f = nullptr;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (f == nullptr)
		return;
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	fprintf(f, "%lld ", ms);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

#define KFC_DIAG_LOG(...) KFCDiagLog(__VA_ARGS__)

// The file of the fault switch <name>: %TEMP%\kbs-diag-fault-<name> (see the header). False when TEMP is not set.
inline bool KFCDiagFaultPath(const char* name, char (&path)[600])
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return false;
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag-fault-%s", temp, name);
	free(temp);
	return true;
}

// Is the fault switch <name> on - does its file exist? Asked of the file's attributes, not by opening it: fopen_s
// shares a file it reads for reading only, so for that moment a test deleting or writing the switch would be
// refused, and a switch the test is still writing would read as off.
inline bool KFCDiagFault(const char* name)
{
	char path[600] = { 0 };
	return KFCDiagFaultPath(name, path) && _access_s(path, 0) == 0;
}

#define KFC_DIAG_FAULT(name) KFCDiagFault(name)

// Turn the fault switch <name> off - its file removed (a one-shot switch the code itself takes away once acted on).
inline void KFCDiagFaultOff(const char* name)
{
	char path[600] = { 0 };
	if (KFCDiagFaultPath(name, path))
		(void)remove(path);
}

// The fault switch <name>'s file as it is - up to 64 KB, a UTF-8 BOM passed over - what KFCDiagFaultText and
// KFCDiagFaultLines read. False when the switch is off.
inline bool KFCDiagFaultBytes(const char* name, std::string& out)
{
	out.clear();
	char path[600] = { 0 };
	if (!KFCDiagFaultPath(name, path))
		return false;
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") != 0 || f == nullptr)
		return false;
	char buffer[512];
	size_t got = 0;
	while (out.size() < 65536 && (got = fread(buffer, 1, sizeof(buffer), f)) > 0)
		out.append(buffer, got);
	fclose(f);
	if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB
		&& static_cast<unsigned char>(out[2]) == 0xBF)
		out.erase(0, 3);
	return true;
}

// The fault switch <name>'s file as text (UTF-8, its first line - a CR or LF ends it, a BOM is passed over) - false when
// the switch is off or its file is empty. (qd-order-file names the file the query dialog's Save Order...
// and Load Order... use instead of asking through Windows' dialogs, which a test cannot press.)
inline bool KFCDiagFaultText(const char* name, std::string& out)
{
	if (!KFCDiagFaultBytes(name, out))
		return false;
	const size_t end = out.find_first_of("\r\n");
	if (end != std::string::npos)
		out.erase(end);
	return !out.empty();
}

// The fault switch <name>'s file as lines (UTF-8, a BOM passed over; CR LF or LF ends a line, an empty line is left out;
// read up to 64 KB) - false when the switch is off. For the switches that carry a list: book-selection (the Book panel's
// selection, KFCBookPanelLookup.cpp) and queries-run (the query files of a run, KFCQuerySequence.cpp).
inline bool KFCDiagFaultLines(const char* name, std::vector<std::string>& out)
{
	out.clear();
	std::string text;
	if (!KFCDiagFaultBytes(name, text))
		return false;
	std::string line;
	text.push_back('\n');
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\r')
			continue;
		if (text[i] != '\n')
		{
			line += text[i];
			continue;
		}
		if (!line.empty())
			out.push_back(line);
		line.clear();
	}
	return true;
}

// The nth whole number (from 0) the fault switch <name>'s file holds, separated by white space - `fallback` when the
// switch is off, or holds fewer numbers, or one that is not a number.
inline int KFCDiagFaultValue(const char* name, int nth, int fallback)
{
	char path[600] = { 0 };
	if (!KFCDiagFaultPath(name, path))
		return fallback;
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") != 0 || f == nullptr)
		return fallback;
	int value = fallback;
	for (int i = 0; i <= nth; ++i)
	{
		int read = 0;
		if (fscanf_s(f, "%d", &read) != 1)
		{
			value = fallback;
			break;
		}
		value = read;
	}
	fclose(f);
	return value;
}

// Milliseconds on a clock that only goes forward - for the timers below.
inline double KFCDiagNowMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#define KFC_CLOCK(var) const double var = KFCDiagNowMs()
#define KFC_SPENT(slot, var) ((slot) += KFCDiagNowMs() - (var))

// WHAT A STRETCH OF KFC MADE INDESIGN DO (the speed-up study - guide vol2-16, Performance Metrics API):
// InDesign's own counters (PerformanceMetricsID.h), read with PerformanceStats::GetValue when this is made and again
// when Since is asked - composition in the layout and in galley (count, time), the change manager's Update calls (the
// observers notified: count, time), undo snapshots (count, time to make), new UIDs and instantiations, and drawing.
// Since writes "lcomp=<n>/<t> gcomp=<n>/<t> notify=<n>/<t> snap=<n>/<t> uid=<n> inst=<n> draw=<t>": the change, in
// whatever unit each counter keeps.
class KFCDiagPerf
{
public:
	KFCDiagPerf() { Take(); }
	void Take()
	{
		for (int i = 0; i < kCount; ++i)
			fV[i] = PerformanceStats::GetValue(Id(i));
	}
	void Since(char* buf, size_t n) const
	{
		unsigned long long d[kCount];
		for (int i = 0; i < kCount; ++i)
			d[i] = (unsigned long long)(PerformanceStats::GetValue(Id(i)) - fV[i]);
		_snprintf_s(buf, n, _TRUNCATE, "lcomp=%llu/%llu gcomp=%llu/%llu notify=%llu/%llu snap=%llu/%llu uid=%llu inst=%llu draw=%llu",
			d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10]);
	}
private:
	enum { kCount = 11 };
	static PerformanceMetricID Id(int i)
	{
		switch (i)
		{
			case 0: return kLayoutCompositionCountPerfID;
			case 1: return kLayoutCompositionTimePerfID;
			case 2: return kGalleyCompositionCountPerfID;
			case 3: return kGalleyCompositionTimePerfID;
			case 4: return kChangeMgrUpdateCallCountPerfID;
			case 5: return kChangeMgrUpdateCallTimePerfID;
			case 6: return kSnapshotCountPerfID;
			case 7: return kNewSnapshotTimePerfID;
			case 8: return kDBNewUIDCountPerfID;
			case 9: return kDBInstantiateCountPerfID;
			default: return kDrawMgrDrawTimePerfID;
		}
	}
	uint64	fV[kCount];
};

// "PHASE <name> begin" now and "PHASE <name> end <ms> <InDesign's counters over it>" when the scope ends.
class KFCDiagPhase
{
public:
	explicit KFCDiagPhase(const char* name) : fName(name), fStart(KFCDiagNowMs())
	{
		KFCDiagLog("PHASE %s begin", fName);
	}
	~KFCDiagPhase()
	{
		char perf[300] = { 0 };
		fPerf.Since(perf, sizeof(perf));
		KFCDiagLog("PHASE %s end %.0f ms %s", fName, KFCDiagNowMs() - fStart, perf);
	}
	KFCDiagPhase(const KFCDiagPhase&) = delete;
	KFCDiagPhase& operator=(const KFCDiagPhase&) = delete;
private:
	const char*	fName;
	double		fStart;
	KFCDiagPerf	fPerf;
};

#define KFC_DIAG_PHASE(var, name) const KFCDiagPhase var(name)

// Counters a test build bumps where something may run more often than it should (guide vol2-15 "do not
// update the user interface from an observer"). One set per plug-in (both are the UI half's).
enum KFCDiagCounterSlot
{
	kKFCDiagTitleRecomputes = 0,	// the panel's tab name recomputed (KFCPanelTitle::Update)
	kKFCDiagFieldMirrors,			// the app bar's mirror of the Find/Change field (KFCAppBarSearchEnter)
	kKFCDiagCounterSlotCount
};
inline int& KFCDiagCounter(KFCDiagCounterSlot which)
{
	static int counters[kKFCDiagCounterSlotCount] = { 0 };
	return counters[which];
}

#else

#define KFC_DIAG_LOG(...) ((void)0)
#define KFC_DIAG_FAULT(name) false
#define KFC_CLOCK(var) ((void)0)
#define KFC_SPENT(slot, var) ((void)0)
#define KFC_DIAG_PHASE(var, name) ((void)0)

#endif // KFC_DIAG

#endif // __KFCDiag_h__

// End, KFCDiag.h.
