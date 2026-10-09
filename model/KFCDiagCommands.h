//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHICH COMMANDS A STRETCH OF KFC MADE INDESIGN RUN, AND HOW MANY (the speed-up study -
//  docs/ai-notes/kfc-speedup-ideas-2026-10-05.md; guide vol2-17's "Trace All Commands", and vol2-15: "commands
//  should operate on lists" - each command stores undo information and notifies). A TEST BUILD'S instrument
//  (the author: "put it on KFC, not borrowed from outside; it is off for the Exchange").
//
//  An ICommandInterceptor (ICommandInterceptor.h - "use with extreme caution from third-party client code") is
//  installed by Arm and taken out by DisarmAndLog, and only in a build with KFC_DIAG, and only while the fault
//  switch perf-commands is on (KFCDiag.h). It counts every command InDesign processes - by class, its name kept
//  from the first one seen - and never refuses one (kCmdNotHandled on every path, as KIDMCP's KIDMCPCmdWatch).
//  Without KFC_DIAG the three functions do nothing; the boss and its implementation stay in the plug-in but are
//  never created (KFC.fr: a resource cannot follow the test build's define, and a boss named there with no
//  implementation behind it would fail to load).
//
//========================================================================================

#ifndef __KFCDiagCommands_h__
#define __KFCDiagCommands_h__

namespace KFCDiagCommands
{
	/** Start counting (a no-op without KFC_DIAG, or with the switch off, or if already counting). */
	void Arm();
	/** Commands processed since Arm (0 when not counting). */
	int Count();
	/** Write "COMMANDS <what> total=<n> <class name>=<count> ..." (the most frequent first) and stop counting. */
	void DisarmAndLog(const char* what);

	/** UNDO-WATCH (the final test, 2026-10-09 - u1-false-redo-undo-other-type4: ANOTHER document's undo history
	    found empty after a script had written the first one). Installed at startup while the fault switch undo-watch
	    is on (KFCDiag.h; read once, at startup), it writes one "UNDOW" line per command InDesign processes - its
	    name, class, undoability and the database of its item list - and, whenever it has changed since the last line
	    that carried it, every open document's undo and redo history (step counts and the top names) as it stands
	    BEFORE that command: a change shows at the first command after whatever made it. Main thread only. A no-op
	    without KFC_DIAG or with the switch off. */
	void WatchUndoArm();
	/** The undo-watch taken out again (the model half's shutdown). */
	void WatchUndoDisarm();
	/** "UNDOH <where> | <every open document's undo and redo history>" - written only while the undo-watch is
	    armed (for the places no command passes: an Undo, a lazy notification). */
	void LogHistory(const char* where);

	/** Counting for the life of the object, whichever way the scope is left. ONE per run (a search, a query run):
	    an inner one would end the outer one's count. Nothing without KFC_DIAG. */
	class Scope
	{
	public:
		explicit Scope(const char* what) : fWhat(what) { Arm(); }
		~Scope() { DisarmAndLog(fWhat); }
	private:
		Scope(const Scope&);
		Scope& operator=(const Scope&);
		const char* fWhat;
	};
}

// The scope as a call site writes it - nothing at all in a build without KFC_DIAG.
#ifdef KFC_DIAG
#define KFC_DIAG_COMMANDS(var, what) const KFCDiagCommands::Scope var(what)
#else
#define KFC_DIAG_COMMANDS(var, what) ((void)0)
#endif

#endif // __KFCDiagCommands_h__

// End, KFCDiagCommands.h.
