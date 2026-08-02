# Kohaku Find/Change (KBS)

> **On the name**: the display name is **Kohaku Find/Change**, after InDesign's own Find/Change. The
> slash is only ever a string table VALUE, never part of a menu path — those are delimited with `:`
> (`SDKDef.h`, `kSDKDefDelimitMenuPath`) and are built from prefix-number keys, not from the display
> name — so it cannot split a path. A `:` or a bare `&` in the name would not be safe; a `/` is.
> The build output is **`KohakuFindChange.pln`** (`kKBSFileName`, which must match the VS project's
> `TargetName`), while the internal plug-in name and the code prefix stay `KohakuBookSearch` / `KBS`
> so nothing in the ID system moves. The SDK's stock `TargetName` would append `.sdk`; KBS overrides
> it in all four configurations, as KESCM does.

An Adobe InDesign C++ plug-in that searches **the front document, or every chapter of the active
book (`.indb`) at once** — the book-wide search the built-in Find/Change cannot do — and shows the
results in a tree panel you can jump from and replace out of.

The same panel runs two other things over the same scope: it finds **missing glyphs** (the boxes
InDesign draws where a font has no glyph for a character) and **overset text** (what did not fit),
neither of which InDesign reports anywhere but preflight.

## The scope, shared by all three commands

- **The `Book Scope` flyout toggle**: off (the default) means the front document, on means every
  chapter of the book. Never a silent fallback between them — with the toggle on and no book open it
  says so rather than quietly searching one document. The search command names the scope it will use
  (`Search Document` / `Search Book`) and the panel's tab carries it (`… - Document` / `… - Book`).
- **Which book**: the one whose tab is frontmost in the Book panel — selecting a tab switches the
  panel but does not make that book "active", so asking for the active book would silently target
  the wrong one. Falls back to the active book when the Book panel is iconised or closed.
- In book scope, chapters that are closed are opened **windowless and with the UI suppressed**
  (read-only walk, dirty-guarded so a chapter never comes out modified) and are **kept open** so row
  jumps and a repeat run do not pay for the load again. They are released when a different book is
  used, when that book is closed, or at shutdown. A chapter that cannot be opened is **named in the
  summary with the book's own reason**, never quietly dropped.
- **A modal progress bar with a Cancel button** covers every run, in both scopes, showing
  `Chapter n / N` over the chapter's name. It is sized in stories (or, for a replace, in hits) rather
  than in chapters, so it keeps moving through a long one. Cancel is honoured between chapters —
  asking inside one would run UI work in the middle of a text walk — so a single huge chapter
  finishes first.
- Only one run at a time: while any of the three is going, the whole flyout is greyed out, and a
  command that arrives from a script is turned away with a message rather than started on top.

## Search

Runs **the official Find/Change dialog's current query**: the find string, the **tab** (Text, GREP or
Glyph) and **all five search options** — Include Master Pages / Locked Layers / Hidden Layers /
Locked Stories / Footnotes. KBS has no search box of its own and writes nothing back to those
settings; the one thing it states is the tab you already have selected, because InDesign's find
engine takes the mode from the last `kFindSearchModeCmdBoss` rather than from the dialog's current
state.

Tabs that search by attribute rather than by text (**Object**, **Colour**) return page items, not
lines, so they are named and turned away. So is **transliterate** (the CJK character-type
conversion), which is a second axis KBS would have to write to the user's settings to drive.

## Find Missing Glyphs

Lists every place a font has no glyph for the character in front of it — what InDesign draws as a
box, and what the official preflight profile calls `Missing glyph`.

It reads the **composed result** (the wax runs), which is what InDesign's own preflight rule and its
on-screen highlight do, rather than searching for a notdef glyph through Find/Change: that search
takes the whole application down on any document holding overset text, by every route there is.
Reading the wax also gives the right notdef id per font and the font's name for free.

- Consecutive boxes are merged into **one row**, so the summary says both numbers:
  `55 missing glyphs in 6 places.`
- The results grow a **font level** — `book → document → font → hit` — because a box means "this
  font has no glyph for this character", so the font is the unit a fix applies to. (The official
  preflight rule offers "Apply a font that has the glyph" for the same reason.)
- **Overset text cannot be checked** — it has no glyphs to read, and neither does the official rule —
  so a scan that met any says `Text in overset cannot be checked.` rather than reporting nothing.

## Find Overset

Lists every place text did not fit: a frame's thread (the red `+`) and a table cell overflowing on
its own (the red dot), nested tables included. Each row names what it is and how much of it there is
— `Frame (370)`, `Table cell (200)` — followed by a peek at the text itself, and clicking the row
scrolls to the `+`.

Matched against the official preflight profile, place for place. Overflow whose `+` sits on no page
at all (a frame on the pasteboard) cannot be jumped to, so it is **counted rather than listed**:
`3 overset places.  1 not on a page.` The official preflight drops those without saying so.

## Reading the results

The tree is **book → document → hit** for a book run and **document → hit** for a document run
(there is no book row when there is no book), with a **font level between the document and its hits**
for a missing-glyph scan. The book row names the book and its total count, permanently, where a
status line would be overwritten by the next message.

Each hit row reads `<locator>  <line>`:

- The **locator** is `P<page>` — Pages-panel style, section aware — plus `(n)` for the hit's ordinal
  within its page when that page holds several, plus `ov` when the match is overset. Flag words are
  appended, spelled out and space-separated, in this order: `hidden` (on a switched-off layer, so
  the page will look empty when you get there), `lock` (locked layer, locked story or locked object,
  so it carries no check box), then `missing` or `refused` (see below), which are drawn in the
  theme's accent colour. So: `P4(1)ov hidden lock`, `P7 missing`.
- The **line** is the text around the match, with the **matched part emphasised**. Long lines are
  ellipsized around the match rather than truncated, so the match is always fully visible with as
  much of its run-up as fits. Colours come from the current UI theme (the match at the theme's text
  colour, the context faded toward the panel background), so it reads correctly in light and dark.

**Clicking a row runs it**: a hit row jumps to the occurrence, a document row brings that document
forward, the book row activates its book. On a jump the chapter is brought to the front (reopened if
you closed it), the view scrolls to the match, and a marker flashes over it for about a second by
inverting the pixels underneath — so it is visible on a red page or a photo alike. It does not select
the text. An overset hit scrolls to the frame's overset `+` instead and raises no marker. With
`Hide Previous Chapter` on, the other displayed clean chapters are closed as the jump lands.

**The up and down arrows walk the whole tree**: they open the row they land on and run it, so
holding the down arrow tours a book chapter by chapter and hit by hit rather than stepping over
closed chapters.

## Replacing what you found

Every hit row of a **search** carries a check box — **unticked**, until you tick it — and
**`Change Checked`** in the flyout replaces only the ticked occurrences. This is the thing the
built-in Find/Change cannot do: it offers Change All or one-at-a-time, never "these 17 of the 20,
across every chapter of the book". A scan's rows carry no check box at all: they report, and there is
nothing on them to rewrite.

- The replacement is **the Find/Change dialog's own "Change to"** (GREP back-references included —
  they are interpreted by InDesign's engine, not by KBS; on the Glyph tab it is the glyph you chose
  there). Like the query, KBS only ever reads those settings.
- **A confirmation names what is about to be written** — the count, the find string and the change
  string — before anything happens. The flyout is easy to open by accident, and those strings live in
  a dialog rather than in this panel, so the prompt is the only place you see what you are agreeing
  to. **Cancel is the default button**, and there is no way to switch the prompt off: a suppressible
  confirmation in front of a destructive rewrite is worth less than a default of Cancel. An empty
  change string is allowed — it deletes the matches — and the prompt spells that out rather than
  showing a blank line. **This prompt is translated** (the panel, its menu and its status line stay
  English, echoing Find/Change's wording).
- On the **Glyph tab** the confirmation is a dialog that **draws the two glyphs themselves**, in the
  fonts that define them, with each font's name and Unicode value under it. A glyph id names nothing
  by itself, and an alternate form drawn as a character would come out as the standard form it
  shares its Unicode with.
- **`Check All` / `Uncheck All` are on the result rows' right-click menu**, and reach exactly the row
  they were popped over: the **book row** means every chapter, a **document row** means that chapter
  alone. They cover **every stored hit**, including any beyond the rows the panel displays, so the
  display cap can never silently shrink what a replace touches.
- **The whole run is ONE undo step.** A single Ctrl+Z puts back every chapter it wrote to, whichever
  chapter you happen to have in front. (It used to open one sequence per chapter, on the belief that
  undo is per document. Measured on the running application, that was actively harmful: undoing in
  one document stripped the step from the other chapters' histories *without* reverting their text.)
- **Cancelling a replace undoes all of it.** The command sequence is aborted, the text goes back to
  where the run found it, the panel is rolled back with it, and the chapters that were clean before
  the run are marked clean again. The price is that finished chapters are thrown away too: breaking
  off a 900-of-1000 run starts over.
- Chapters that received a replacement are **opened in a window and left unsaved** — overwriting
  your files stays your decision.
- **A checked hit that is not replaced is always counted and named**, never allowed to make the
  total quietly come up short. Four ways that happens: `lock` (locked content — InDesign can search
  it but offers no way to change it, so KBS follows), `missing` (the text is no longer where the
  search left it), `refused` (the replace command itself would not run there), and a chapter the
  safety ceiling cut short (named in the summary, since nothing was found out about those rows).
  A chapter the text walker would not run on at all is named too.
- **Locked hits are listed but not selectable**: they get no check box at all, rather than one that
  would quietly do nothing, and `Check All` skips them.
- Editing between the search and the replace is caught rather than written over. A row is only
  replaced when the same occurrence is still there — same story, same position, same text — and the
  pass's own shifts are cancelled out, so a change string of a different length does not make every
  later hit look moved. Jumping to a row checks the same thing, so **the panel can tell you a row
  has gone stale before you press `Change Checked`**.
- **After a replace the panel becomes a report of that run**: the rows it changed (with the new text
  emphasised the way a match is), the rows it was asked about and left alone with the reason on the
  locator, and the locked rows that account for a search turning up more than the replace was
  allowed to touch. The rows you had unticked are dropped, and so are chapters left with nothing.
  No row on a report offers a check box, and the locator drops the within-page ordinal (`P1`, not
  `P1(3)`) because with the untouched hits gone the numbering would only be gaps. Rows still jump.
- **Undoing a replacement does not un-do the panel** — the report still describes what was replaced
  until you run the search again.

## Results going stale

- **Close the searched document** and a document-scope result set is retired at once (a row naming a
  closed document would still jump, and `Change Checked` would still reopen it to write in it).
- **Close the book** and a book-scope result set is retired, and the chapters KBS opened windowless
  are closed with it — leaving them open would keep their `.indd` files locked for the rest of the
  session. Neither happens while a run is going: the question is deferred until it finishes.
- A book chapter you close yourself is *not* dropped from the results: it carries its file, so KBS
  reopens it when a jump or a replace needs it. Nothing is written blind — the same-occurrence test
  above still has to pass.

## Limits

- The panel displays the first **5000** hit rows (`kKBSDisplayHitLimit`); the model still holds every
  hit, so checking and replacing are never capped by what is on screen. The boundary chapter's row
  shows `(shown / total)`.
- A **search** stops collecting at **10000** hits across the whole run (`kKBSCollectHitLimit`), which
  bounds the result set rather than only its display. The summary says so and asks you to narrow the
  query. The two scans have no such ceiling — what they find is what is wrong with the document, and
  a document with 10000 boxes in it needs all of them named.
- ⚠ **Do not switch Find/Change tabs between a search and `Change Checked`**: the replace re-walks
  each chapter with whatever mode is current, so a different tab returns a different set of matches
  and the rows stop lining up. KBS notices and refuses the run rather than writing anything wrong,
  but you have to search again.
- Cancel is asked at chapter boundaries, so a **document-scope** run cannot be interrupted partway.

## Reading the panel from a script

Two read-only properties on the application object, for verification (a search or a replace reports
in one line, and reading that line off the screen cannot be automated):

    app.kbsStatus     // "1234 hit(s) in 5 of 12 chapter(s) - book "x.indb"."
    app.kbsResults    // every row the panel is holding, tab-separated, one hit per line

Both answer with the panel closed.

## Layout

- Plug-in sources are flat in this repo (`.cpp` / `.h` / `.fr` / `.rc` / `.png`).
- `_buildproj/` holds a backup of the build files from the SDK build tree (`build/win/prj/`), which
  is outside this repository — see `_buildproj/README.md` for how to restore and build.
- `KBS_jaJP.fr` **must stay CP932 (Shift-JIS)**, not UTF-8 — this SDK's ODFRC lexes it as CP932 and
  a UTF-8 character breaks the string it is in. See the header comment in that file before editing.
- Replacing a `.png` alone does not reach a build: the icon compile step watches `KBS.fr`'s
  timestamp, not the image's. Touch `KBS.fr` after swapping a picture.

## Status

Version **1.0.0**. Built as a vertical-slice progression (skeleton → book search → result tree →
jump → polish → checked replace → progress bars → stale-hit handling → missing-glyph scan → overset
scan), verified on the real application at each step.

Prefix `0x205698`. Menu: `Plug-Ins ▸ KohakuNekotarou ▸ Kohaku Find/Change`.
