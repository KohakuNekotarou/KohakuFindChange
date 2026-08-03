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
  chapter of the book. Never a silent fallback between them. The search command names the scope it
  will use (`Find in Document` / `Find in Book`) and the panel's tab carries it
  (`… - Document` / `… - Book`).
- **Nothing to run on, nothing to click**: with no front document in document scope — or no book open
  in book scope — all three commands are **greyed out**, rather than starting and reporting that
  there was nothing to search. The name still says which scope they would have used.
- **Which book**: the one whose tab is frontmost in the Book panel — selecting a tab switches the
  panel but does not make that book "active", so asking for the active book would silently target
  the wrong one. Falls back to the active book when the Book panel is iconised or closed.
- In book scope, chapters that are closed are opened **windowless and with the UI suppressed**
  (read-only walk, dirty-guarded so a chapter never comes out modified) — **one at a time, and closed
  again the moment that chapter has been walked**. A run therefore holds at most one chapter of its
  own and leaves nothing open behind it, so no `.indd` stays locked. Chapters you had open yourself
  are never touched. What that costs is a document load the first time you click a row into a chapter
  (rows carry their chapter's file, so the jump reopens it); what it buys is that searching a
  twenty-chapter book no longer leaves twenty hidden documents in the session. A chapter that cannot
  be opened is **named in the summary with the book's own reason**, never quietly dropped.
- **A modal progress bar with a Cancel button** covers every run, in both scopes, showing
  `Chapter n / N` over the chapter's name. Every chapter gets an equal slice of the bar — a chapter's
  size cannot be asked for before it is opened — and the search subdivides its own slice by story, so
  it keeps moving through a long chapter. (A replace is still sized in hits.) Cancel is honoured between chapters —
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
  showing a blank line. **This prompt is translated** — as is the extra warning that follows it when
  you tick `Save each chapter after replace` — while the panel, its menu and its status line stay
  English, echoing Find/Change's wording. Those two are where you authorise a rewrite of your text
  and an overwrite of your files, which is what earns them a translation.
- On the **Glyph tab** that same prompt **draws the two glyphs themselves** instead of quoting
  strings, in the fonts that define them, with each font's name and Unicode value under it. A glyph
  id names nothing by itself, and an alternate form drawn as a character would come out as the
  standard form it shares its Unicode with. (If the fonts cannot be resolved — a ROS-group query
  carries none — it falls back to quoting, so a confirmation that cannot be *drawn* is never a
  reason the replace cannot *run*.)
- **`Check All` / `Uncheck All` are on the result rows' right-click menu**, and reach exactly the row
  they were popped over: the **book row** means every chapter, a **document row** means that chapter
  alone. They cover **every stored hit**, including any beyond the rows the panel displays, so the
  display cap can never silently shrink what a replace touches.
- **How a run is wrapped depends on whether you asked it to save**, because saving is what makes a
  chapter settled. The two behave differently enough to be worth knowing apart.
- **Without saving — the whole run is ONE undo step.** A single Ctrl+Z puts back every chapter it
  wrote to, whichever chapter you happen to have in front. (It used to open one sequence per chapter,
  on the belief that undo is per document. Measured on the running application, that was actively
  harmful: undoing in one document stripped the step from the other chapters' histories *without*
  reverting their text.) Every chapter that has work is opened before the first character is written,
  and they all stay open and unsaved afterwards — overwriting your files stays your decision.
  **Cancelling undoes all of it**: the text goes back to where the run found it, the panel is rolled
  back with it, and the chapters that were clean before the run are marked clean again. The price is
  that finished chapters are thrown away too — breaking off a 900-of-1000 run starts over.
- The confirmation offers **`Save each chapter after replace`** when you want it done for you. It
  starts **off every time**: a replace overwrites files, so it is asked per run rather than remembered
  anywhere. Ticking it brings up **one more warning**, with Cancel as the default button — a saved
  file is the one thing here that nothing takes back, and it changes what Cancel means below.
- **With saving — one chapter at a time.** Each is opened, replaced, saved, and handed straight back,
  so a run of any size holds **at most one chapter of its own**. A twenty-chapter book used to load all
  twenty before writing a single character, which is more than a modest machine has to give. Every
  document a replacement lands in is saved, whoever opened it; documents nothing landed in are never
  touched (if one of those is dirty, that edit is somebody else's). Chapters KBS opened are **closed
  once saved**, whatever `Hide Previous Chapter` says — that toggle is about jumping. Documents you had
  open yourself are never closed. Rows still jump: a closed chapter is reopened on the way.
- **Cancelling a saving run stops it where it stands.** The chapters it finished are on disk and
  nothing can take them back, so they stay — and the panel goes on showing their rows as replaced,
  because that is what those files now contain. The chapters it never reached keep their ticks and say
  **`cancelled`** beside their name. Searching again picks up where it left off: what was replaced no
  longer matches, so only the rest comes back.
- A chapter that **cannot be written** — read-only, or a document that has never been saved at all —
  is named in the status line, keeps its replacements, and is **left open with a window** so you can
  deal with it by hand. The chapters that did save are still closed.
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
  No row on a report offers a check box. The within-page ordinal is **renumbered over what is left**,
  so a page reads "the first replacement, the second, the third" (`P1(1)`, `P1(2)`) rather than
  carrying the search's numbering with gaps where the unticked rows were. Rows still jump.
- **Undoing a replacement does not un-do the panel** — the report still describes what was replaced
  until you run the search again.

## Saving the results

**`Save Results...`** in the flyout writes what the panel is holding to a tab-separated text file —
one line per hit, ready to paste into a spreadsheet. It is greyed out while there is nothing to save.

    Kohaku Find/Change
    Query: cat  (Text)
    Book: savetest.indb
    Summary: 9 hit(s) in 3 of 3 chapter(s) - book "savetest.indb".
    Rows: 9

    <Document>  <Page>  <No>  <Text>              <Font>  <Flags>
    ch1.indd    1       1     ...the cat sat...
    ch1.indd    2             ...another cat...           lock

- **Every stored hit is written**, including those past the panel's 5000-row display limit, so `Rows:`
  can be larger than what is on screen.
- The heading names the command that produced the rows (`Kohaku Find/Change`, `Find Missing Glyphs`,
  `Find Overset`, or `Kohaku Find/Change (after Change Checked)`), the query with the tab it was typed
  on, the book or document, and **the panel's own summary line verbatim** — so the file can never
  contradict what the panel said.
- **`<Page>` is the page number alone** so a spreadsheet can sort on it; the `ov` / `hidden` / `lock` /
  `missing` / `refused` / `replaced` flags are in `<Flags>`, spelled exactly as the panel's locator
  spells them. `<No>` is the within-page ordinal (empty when the page holds a single row), and
  `<Font>` is filled in by the glyph scan only.
- **Tabs and line breaks inside the text are flattened to a single space** — a match can run across a
  paragraph break, and a real newline in a cell would split the row.
- **UTF-8 with a BOM and CRLF ends**, so Notepad and Excel both open Japanese text correctly.
- The suggested name is `KohakuFindChangeReport_<document or book>_<what was done>.txt` —
  `KohakuFindChangeReport_ch1_FindText.txt`, `..._savetest_FindGrep.txt`,
  `..._glyphbook_MissingGlyphs.txt`, `..._ch1_ChangeText.txt`.
- A save that worked says nothing (the file is where you put it) and a cancelled chooser does nothing
  at all; only a write that failed reaches the status line.

## Results going stale

- **Close the searched document** and a document-scope result set is retired at once (a row naming a
  closed document would still jump, and `Change Checked` would still reopen it to write in it).
- **Close the book** and a book-scope result set is retired, and any chapter KBS still has open for it
  is closed with it — leaving one open would keep its `.indd` locked for the rest of the session. (A
  run closes its own chapters as it goes, so what this catches is a chapter a row jump or a replace
  reopened.) Neither happens while a run is going: the question is deferred until it finishes.
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
