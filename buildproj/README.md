# buildproj — build files backup

The real build files live in the InDesign SDK build tree at `build/win/prj/`, which is **outside
this repository**. This folder keeps a copy of them so the project's build customizations are not
lost when only the source is pushed. `build/win/prj/` is authoritative; this is the backup — after
editing a build file there, copy it back here and commit.

Since 2026-10-01 Kohaku Find/Change is **two plug-ins** (the model/UI split the SDK guide asks for —
Programming Guide vol. 1, chapters 6 and 7):

| | Project | Sources | Output | Kind |
|---|---|---|---|---|
| model | `KohakuFindChange.vcxproj` | `model/` | `KohakuFindChange.pln` | the engines, the results, the tracked-change signature, the jump marker's adornment |
| UI | `KohakuFindChangeUI.vcxproj` | `ui/` | `KohakuFindChangeUI.pln` | the panel, its menus and actions, everything the user sees |

Both `.pln` files (and both `(… Resources)` folders) have to be installed together; the UI half
declares a `PluginDependency` on the model half, never the other way round.

## Files kept here
- `KohakuFindChange.vcxproj` / `.vcxproj.filters` — the model half's MSVC project.
- `KohakuFindChangeCPP.rsp` / `KohakuFindChangeODFRC.rsp` — its compiler / ODFRC response files.
- `KohakuFindChangeUI.vcxproj` / `.vcxproj.filters` — the UI half's MSVC project.
- `KohakuFindChangeUICPP.rsp` / `KohakuFindChangeUIODFRC.rsp` — its response files. The C++ one adds
  `/I ..\..\..\source\sdksamples\KBS\model`: the UI includes the headers the halves share
  (`KBSBoundaryID.h`, `KBSModelTypes.h`, `KBSLoc.h`, `IKBS*.h`), which live in `model/` and nowhere else.
- The four `.sdk.props` per project (`KBS*.sdk.props`, `KFCUI*.sdk.props`) are NOT kept: they are the
  DollyXs boilerplate, identical between projects.

## Restore into a clean SDK checkout
1. Copy the eight files above into `build/win/prj/`, and make `KFCUI<Debug|Release><Win32|X64>.sdk.props`
   as copies of the `KBS…` ones.
2. Copy this repo's `model/` and `ui/` folders into `source/sdksamples/KBS/`. The SDK folder keeps the short
   name `KBS` - it is not renamed along with the project. The sources moved a level down into `source/` on
   2026-08-10 and were split into `model/` and `ui/` on 2026-10-01; everything a folder includes resolves
   against that folder (and the UI's against `model/` as well), so each has to stay together.
3. Register both projects in `build/win/prj/SDKSamples.sln`:
   - `KohakuFindChange` `{DD125A1E-99DD-4D59-B001-DEB4D37D34C5}` and `KohakuFindChangeUI`
     `{7C3E4B2A-5D91-4F0B-9E2C-1A6B8D4F3E71}`: a `Project(...)` / `EndProject` block each, and
   - eight `{GUID}.<Debug|Release>|<x64|x86>.<ActiveCfg|Build.0>` lines each in
     `GlobalSection(ProjectConfigurationPlatforms)`.
   The matching `<ProjectGuid>` is already in each `.vcxproj`.

## Build customizations captured here (do not lose)
- **Link lists**: the model links `$(MODEL_PLUGIN_LINKLIST);$(BoostThreadLib)` (no WidgetBin - a model
  plug-in must not), the UI `$(UI_PLUGIN_LINKLIST);user32.lib` (`DV_WidgetBin.lib` comes in the list -
  the self-drawing result cell and message area need it; `user32.lib` for the Win32 window work).
- **Each `.fr` build watches its FactoryList** (`AdditionalInputs`): ODFRC does not see a header included
  inside `#ifdef __ODFRC__`, so a new implementation would otherwise not reach the resources.
- `common/SDKFileHelper.cpp` is compiled into both (the model's marker / book scope and the UI's Book panel
  lookup use it).
- Every source is a plain `ClCompile`; only `VCPlugInHeaders.cpp` creates the precompiled header.
- `WindowsTargetPlatformVersion` is `10.0`.

Build: Release|x64, with InDesign closed (an open InDesign locks the .pln -> LNK1104). After moving a
`.fr` to another folder, **rebuild** (`/t:Rebuild`): an incremental build stops on ODFRC's "Previous .fr
file ... do not match", and the retry ships a `.pln` with no resources at all.
