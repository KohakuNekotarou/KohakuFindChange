# Kohaku Search Panel (KBS)

> **On the name**: the display name is **Kohaku Search Panel**. The `.pln` file name, the VS project
> and the code prefix stay `KohakuBookSearch` / `KBS` — the same way KESCL kept its old prefix after
> being renamed — so the build output is still `KohakuBookSearch.sdk.pln`.

An Adobe InDesign C++ plug-in that searches **the front document, or every chapter of the active
book (`.indb`) at once** — the book-wide search the built-in Find/Change cannot do — and shows the
results in a two-level tree panel.

## What it does
- **Takes the scope from the `Book Scope` flyout toggle**: off (the default) searches the front
  document, on searches every chapter of the active book. The search command names the scope it will
  use (`Search Document` / `Search Book`). With the toggle on and no book open it says so instead of
  quietly searching one document.
- Runs the search with **the official Find/Change dialog's current query, mode included** (Text or
  GREP). KBS sets nothing on the Find/Change panel: whatever you typed there (and whichever tab) is
  what it searches, across the whole scope. There is no separate search box.
- In book scope, opens closed chapters windowless to search them, then releases them (read-only
  walk, guarded so a chapter never comes out modified).
- Presents hits as a tree: **chapter → hit line**. Each hit row reads `P<page>(<n>) <line>` — the
  page (Pages-panel style, section aware; `(n)` = the within-page ordinal when a page holds several
  matches; `ov` = overset), then the line's text with the **matched part emphasized**. Colours are
  taken from the current UI theme (the matched text at the theme's text colour, the surrounding
  context faded toward the panel background), so it looks right in both light and dark interfaces.

## Replacing what you found

Every hit row carries a **check box**, ticked by default. Untick the ones you want to leave alone
and run **`Change Checked`** from the flyout — only the ticked occurrences are replaced. This is the
thing the built-in Find/Change cannot do: it offers Change All or one-at-a-time, never "these 17 of
the 20, across every chapter of the book".

- The replacement text is **the Find/Change dialog's own "Change to" field** (GREP back-references
  included — they are interpreted by InDesign's engine, not by KBS). Like the search query, KBS only
  ever reads those settings, never writes them.
- **A confirmation prompt names the find string, the change string and the count** before anything is
  written, with **Cancel as the default button** — the flyout is easy to open by accident. An empty
  change string is allowed (it deletes the matches) but the prompt says so explicitly.
- `Check All` / `Uncheck All` cover **every stored hit**, including any beyond the 500 the panel
  displays, so the display cap can never silently shrink what a replace touches.
- **Undo is one step per chapter**: a single Ctrl+Z in a chapter takes back everything the replace
  did there.
- Chapters that received a replacement are **opened in a window and left unsaved** — overwriting your
  files stays your decision.
- A chapter whose text no longer matches the results (edited since the search, or the query changed)
  is **reported rather than replaced at guessed positions**; search again to refresh.
- **After a replace the panel turns into a list of what changed.** Only the replaced rows remain (a
  chapter nothing landed in drops out of the tree), they carry **no check box** — there is nothing
  left to select — and the text that replaced the match is emphasised exactly the way a match is,
  so the result reads like the search did. Their locator drops the within-page ordinal (`P1`, not
  `P1(3)`): with the untouched hits gone the numbering would only be gaps. Rows still jump.
- **Undoing a replacement does not un-do the panel** — the list still shows what was replaced until
  you run the search again.

## Layout
- Plug-in sources are flat in this repo (`.cpp` / `.h` / `.fr` / `.rc`).
- `_buildproj/` holds a backup of the build files from the SDK build tree (`build/win/prj/`), which
  is outside this repository — see `_buildproj/README.md` for how to restore and build.

## Status
Built as a vertical-slice progression (skeleton → book search → result tree → jump → polish →
checked replace). The result tree with themed match highlighting, the hit-row jump with its marker,
overset locating, the document / book scope toggle and `Change Checked` are all in place and
verified on the real application.

Prefix `0x205698`. Menu: `Plug-Ins ▸ Kohaku Plug-Ins ▸ Kohaku Search Panel`.
