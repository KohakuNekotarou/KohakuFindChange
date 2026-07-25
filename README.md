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

## Layout
- Plug-in sources are flat in this repo (`.cpp` / `.h` / `.fr` / `.rc`).
- `_buildproj/` holds a backup of the build files from the SDK build tree (`build/win/prj/`), which
  is outside this repository — see `_buildproj/README.md` for how to restore and build.

## Status
Built as a vertical-slice progression (skeleton → book search → result tree → jump → polish). The
result tree with themed match highlighting, the hit-row jump with its marker, overset locating and
the document / book scope toggle are all in place.

Prefix `0x205698`. Menu: `Plug-Ins ▸ Kohaku Plug-Ins ▸ Kohaku Search Panel`.
