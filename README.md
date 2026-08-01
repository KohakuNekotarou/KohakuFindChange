# Kohaku Find/Change (KBS)

> **On the name**: the display name is **Kohaku Find/Change**, after InDesign's own Find/Change. The
> slash is only ever a string table VALUE, never part of a menu path — those are delimited with `:`
> (`SDKDef.h`, `kSDKDefDelimitMenuPath`) and are built from prefix-number keys, not from the display
> name — so it cannot split a path. A `:` or a bare `&` in the name would not be safe; a `/` is.
> The `.pln` file name, the VS project
> and the code prefix stay `KohakuBookSearch` / `KBS` — the same way KESCL kept its old prefix after
> being renamed — so the build output is `KohakuBookSearch.pln`. (The SDK's stock `TargetName` would
> make that `KohakuBookSearch.sdk.pln`; KBS overrides it in all four configurations, as KESCM does,
> so the shipped file is not named after the sample framework it was built from.)

An Adobe InDesign C++ plug-in that searches **the front document, or every chapter of the active
book (`.indb`) at once** — the book-wide search the built-in Find/Change cannot do — and shows the
results in a tree panel you can jump from and replace out of.

## What it does

- **Takes the scope from the `Book Scope` flyout toggle**: off (the default) searches the front
  document, on searches every chapter of the book. The search command names the scope it will use
  (`Search Document` / `Search Book`) and the panel's own tab carries it (`… - Document` / `… - Book`).
  With the toggle on and no book open it says so instead of quietly searching one document.
- **Which book**: the one whose tab is frontmost in the Book panel — selecting a tab switches the
  panel but does not make that book "active", so asking for the active book would silently target
  the wrong one. Falls back to the active book when the Book panel is iconised or closed.
- Runs the search with **the official Find/Change dialog's current query**: the find string, the
  **mode** (Text or GREP), and **all five search options** — Include Master Pages / Locked Layers /
  Hidden Layers / Locked Stories / Footnotes. KBS sets nothing on the Find/Change panel and has no
  search box of its own; whatever you typed there, on whichever tab, is what it searches.
- In book scope, opens closed chapters **windowless and with the UI suppressed** to search them
  (read-only walk, dirty-guarded so a chapter never comes out modified) and **keeps them open** so
  row jumps and a repeat search do not pay for the load again. They are released when a different
  book is searched, when that book is closed, or at shutdown.
- **A modal progress bar with a Cancel button** covers a book-wide search and a book-wide replace,
  showing `Chapter n / N` over the chapter's name. Cancel is honoured between chapters (asking
  inside one would run UI work in the middle of a text walk), so a huge single chapter finishes
  first. A document-scope run is one step and raises no bar.

## Reading the results

The tree is **book → document → hit** for a book search, and **document → hit** for a document
search (there is no book row when there is no book). The book row names the book and its total hit
count, permanently, where a status line would be overwritten by the next message.

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

**Clicking a hit row jumps to it**: the chapter is brought to the front (reopened if you closed it),
the view scrolls to the match, and a marker flashes over it for about a second by inverting the
pixels underneath — so it is visible on a red page or a photo alike. It does not select the text.
An overset hit scrolls to the frame's overset `+` instead. With `Hide Previous Chapter` on, the
other displayed clean chapters are closed as the jump lands.

## Replacing what you found

Every hit row carries a **check box**, ticked by default, and **`Change Checked`** in the flyout
replaces only the ticked occurrences. This is the thing the built-in Find/Change cannot do: it
offers Change All or one-at-a-time, never "these 17 of the 20, across every chapter of the book".

- The replacement text is **the Find/Change dialog's own "Change to" field** (GREP back-references
  included — they are interpreted by InDesign's engine, not by KBS). Like the query, KBS only ever
  reads those settings, never writes them.
- **A confirmation prompt names the find string, the change string and the count** before anything
  is written — the flyout is easy to open by accident, and those strings live in a dialog rather
  than in this panel, so the prompt is the only place you see what is about to happen. It offers a
  **`Don't show again`** check box; once ticked, `Change Checked` runs straight away (revive it with
  Preferences ▸ General ▸ Reset All Warning Dialogs). An empty change string is allowed — it deletes
  the matches — and the prompt spells that out rather than showing a blank line. **This prompt is
  translated** (the panel, its menu and its status line stay English, echoing Find/Change's wording).
- `Check All` / `Uncheck All` cover **every stored hit**, including any beyond the rows the panel
  displays, so the display cap can never silently shrink what a replace touches.
- **The whole run is ONE undo step.** A single Ctrl+Z puts back every chapter it wrote to, whichever
  chapter you happen to have in front. (It used to open one sequence per chapter, on the belief that
  undo is per document. Measured on the running application, that was actively harmful: undoing in
  one document stripped the step from the other chapters' histories *without* reverting their text.)
- **Cancelling a replace undoes all of it.** The command sequence rolls the text back to where the
  run found it and the panel is rolled back with it, so stopping means stopping — no half-changed
  book with nothing on screen to say where the line fell. The price is that finished chapters are
  thrown away too: breaking off a 900-of-1000 run starts over.
- Chapters that received a replacement are **opened in a window and left unsaved** — overwriting
  your files stays your decision.
- **A checked hit that is not replaced is always counted and named**, never allowed to make the
  total quietly come up short. Four ways that happens: `lock` (locked content — InDesign can search
  it but offers no way to change it, so KBS follows), `missing` (the text is no longer where the
  search left it), `refused` (the replace command itself would not run there), and a chapter the
  safety ceiling cut short (named in the summary, since nothing was found out about those rows).
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
  session.
- A book chapter you close yourself is *not* dropped from the results: it carries its file, so KBS
  reopens it when a jump or a replace needs it. Nothing is written blind — the same-occurrence test
  above still has to pass.

## Limits

- The panel displays the first **5000** hit rows (`kKBSDisplayHitLimit`); the model still holds every
  hit, so checking and replacing are never capped by what is on screen. The boundary chapter's row
  shows `(shown / total)`.
- Collection itself stops at **10000** hits across the whole search (`kKBSCollectHitLimit`), which
  bounds the result set rather than only its display. The summary says so and asks you to narrow the
  query.
- KBS does not interpret the Find/Change **tab** at all: it passes the dialog's options through
  untouched, so whatever InDesign's own find engine makes of them is what happens. **Text, GREP and
  Glyph all work** (measured 2026-07-30). A tab that searches by attribute rather than by text
  (Object, Colour) leaves no find string for KBS to check, so the search stops and says nothing is
  set.
- ⚠ **Do not switch tabs between a search and `Change Checked`**: the replace re-walks each chapter
  with whatever mode is current, so a different tab returns a different set of matches and the rows
  stop lining up. Nothing wrong is written — a row is only replaced when the same text is still at
  the same place — but you will get a lot of "not found" instead of replacements.

## Layout

- Plug-in sources are flat in this repo (`.cpp` / `.h` / `.fr` / `.rc`).
- `_buildproj/` holds a backup of the build files from the SDK build tree (`build/win/prj/`), which
  is outside this repository — see `_buildproj/README.md` for how to restore and build.
- `KBS_jaJP.fr` **must stay CP932 (Shift-JIS)**, not UTF-8 — this SDK's ODFRC lexes it as CP932 and
  a UTF-8 character breaks the string it is in. See the header comment in that file before editing.

## Status

Version **1.0.0**. Built as a vertical-slice progression (skeleton → book search → result tree →
jump → polish → checked replace → progress bars → stale-hit handling), verified on the real
application at each step.

Prefix `0x205698`. Menu: `Plug-Ins ▸ KohakuNekotarou ▸ Kohaku Find/Change`.
