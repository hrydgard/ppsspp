# Slang Shader Support — Phase 4 (Category Browser UI + Parameters) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user browse the imported slang shaders by category, select a preset to activate it (writing the active-preset config the renderer already reads), and adjust that preset's `#pragma parameter` values with live sliders that persist — all through one cross-platform screen reached from Graphics settings.

**Architecture:** Add `Core/Slang/SlangPresetLibrary.{h,cpp}` — a device-free index of the imported `slang/` tree (scans `GetSlangShaderDir()`, groups presets by first path segment = category, enumerates each preset's parameters by text-parsing its `.slangp` + `.slang` files with the existing `ParseSlangPreset`/`SplitSlangSource`, no GPU compile). Add `UI/SlangShaderScreen.{h,cpp}` — a `UIBaseDialogScreen` with an Import/Update button (reusing Phase 3's `g_SlangImporter`), a search box, a category → preset browser, a "None" entry, and per-preset parameter sliders. Selection writes `g_Config.sSlangShaderPreset` (the absolute `.slangp` path the renderer already loads). Parameter tweaks persist in a new `g_Config.mSlangParams` map (manual ini-section, mirroring `mPostShaderSetting`) and are fed into `SlangFilterChain::Run`'s `UserParameter` binding as overrides. One row in `UI/GameSettingsScreen.cpp` pushes the screen, replacing the temporary Phase 3 "Import" button.

**Tech Stack:** C++17; PPSSPP `Common/UI` (`UIBaseDialogScreen`, `ScrollView`, `Choice`, `CollapsibleSection`, `PopupSliderChoiceFloat`, `SearchBar`/`ViewSearch`); `I18NCat::GRAPHICS`; existing `GPU/Common/Slang/` parser (`ParseSlangPreset`, `SplitSlangSource`, `ResolveSlangIncludes`); `Core/Config` manual map (de)serialization; PPSSPP `unittest` harness.

## Global Constraints

- **License header:** every new file keeps the PPSSPP GPL 2.0 header, year `2026-` (copy the style from existing `Core/Slang/*` / `GPU/Common/Slang/*` files).
- **Device-free library:** `SlangPresetLibrary` MUST NOT compile shaders or touch the GPU. It only reads files and text-parses them (`ParseSlangPreset` + read each `.slang` + `ResolveSlangIncludes` + `SplitSlangSource`). It is unit-testable on desktop against a fake tree.
- **Scan the right directory:** the library scans `GetSlangShaderDir()` (Phase 3: resolves to the NATIVE app-private dir `g_extFilesDir/slang` on Android, `DIRECTORY_CUSTOM_SHADERS/slang` elsewhere). Do NOT scan `DIRECTORY_CUSTOM_SHADERS` directly — imported slang shaders live under `GetSlangShaderDir()`.
- **Category rule (from design §10.1):** the first path segment under the slang root is the category (e.g. `crt/crt-royale.slangp` → category `crt`). A `.slangp` directly at the root has category "" → bucket it under a synthetic "misc"/"(root)" category rather than dropping it.
- **Selection sink:** activating a preset sets `g_Config.sSlangShaderPreset` to the preset's absolute `.slangp` path; "None" sets it to `""`. The renderer (`FramebufferManagerCommon::UpdateSlangChain`) already reads this and reloads on path change — no renderer change needed for selection.
- **Mutual exclusivity (design §5.1/§10.2):** selecting a slang preset clears the legacy post-shader list (`g_Config.vPostShaderNames` → `{"Off"}`); the two systems never both process a frame. (The present path already runs slang first if set; this constraint keeps the UX unambiguous.)
- **Parameter persistence:** `g_Config.mSlangParams` is `std::map<std::string, float>` keyed by `"<presetPath>|<paramName>"`. It is NOT auto-persisted by `ConfigSetting` — add manual Load/Save in a dedicated `[SlangParams]` ini section exactly like `mPostShaderSetting` (`Core/Config.cpp:1411` load / `:1518` save). Legacy `vPostShaderNames` / `mPostShaderSetting` are untouched.
- **Zero regression:** the legacy post-shader UI and config are untouched; the slang render path for a preset with no parameter overrides must render identically to today (override map empty → use each param's `initial`, exactly current behavior).
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Build + test env (unchanged from Phases 2/3):**
  - Desktop unit build + run: `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest <Name>` (**exit 0 = pass**, no banner). Tests are `bool TestXxx()` (unittest/UnitTest.h macros), registered in `unittest/UnitTest.cpp` (forward decl + `TEST_ITEM`).
  - UI files (`UI/*.cpp`) are NOT compiled by the unittest target — verify UI-touching tasks with the **Android APK build**: `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=slang-dev --console=plain`; install `adb install -t -r android/build/intermediates/apk/normal/debug/android-normal-debug.apk`. Device: Adreno/Vulkan; imported shaders at `/storage/emulated/0/Android/data/org.ppsspp.ppsspp/files/slang`.
  - New non-UI files (`Core/Slang/*.cpp`) must be added to the SAME seven build lists Phase 3 used: `CMakeLists.txt`, `Core/Core.vcxproj`, `Core/Core.vcxproj.filters`, `libretro/Makefile.common`, `android/jni/Android.mk`, `UWP/CoreUWP/CoreUWP.vcxproj`, `UWP/CoreUWP/CoreUWP.vcxproj.filters` (grep `Core/Slang/SlangPaths.cpp` to find every list).

## Verbatim reference: existing infrastructure this plan builds on

**Slang data (device-free enumeration)** — `GPU/Common/Slang/`:
- `struct SlangParamDesc { std::string name; float initial=0; float minimum=0; float maximum=1; float step=0.01f; };` (`SlangPreset.h:44`). **No description field yet** — Task 1 adds one.
- `struct SlangPreset { Path basePath; std::vector<SlangPassDesc> passes; std::vector<SlangParamDesc> params; std::vector<SlangLutDesc> luts; int feedbackPass; };` (`SlangPreset.h:76`).
- `bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error);` (`SlangpParser.h:27`) — parses `.slangp` text only; fills passes/luts; clears `out->params` (does NOT read `.slang` pragmas).
- `struct SlangSource { std::string vertex, fragment, name; std::vector<SlangParamDesc> params; SlangFbFormat format; };` and `bool SplitSlangSource(const std::string &src, SlangSource *out, std::string *error);` (`SlangpParser.h:38`) — pure text; yields `#pragma name` (→ `name`) and one `SlangParamDesc` per `#pragma parameter`.
- `#pragma parameter NAME "Description" INIT MIN MAX [STEP]` grammar (`SlangpParser.cpp:223`). `ParseParameterPragma` currently keeps NAME/INIT/MIN/MAX/STEP and **discards "Description"**.
- Include resolution: `ResolveSlangIncludes(src, dir, reader, &out, &error)` (used in `SlangFilterChain.cpp:251`).
- The per-preset full param list is assembled in `SlangFilterChain::Load` by merging each pass's `src.params` into `preset_.params`, `.slangp`-level winning (`SlangFilterChain.cpp:264-275`).
- `UserParameter` value binding in `Run` (`SlangFilterChain.cpp:559-566`): finds the member name in `preset_.params`, writes `p.initial`. **This is where Task 5 injects overrides.**

**Renderer selection sink** — `FramebufferManagerCommon::UpdateSlangChain` (`FramebufferManagerCommon.cpp:125-151`): reads `g_Config.sSlangShaderPreset` (absolute `.slangp` path; empty = off), caches on `slangChainPresetPath_`, reloads when the path changes.

**Config** (`Core/Config.{h,cpp}`): `std::string sSlangShaderPreset` (`Config.h:380`, registered `Config.cpp:734`, `CfgFlag::PER_GAME`). Map precedent `std::map<std::string,float> mPostShaderSetting` (`Config.h:372`), saved `Config.cpp:1518` / loaded `:1411` via `iniFile.GetOrCreateSection(...)` + `section->Set(k,v)` / `section->ToMap()` + `std::stof`.

**UI building blocks** (all in `Common/UI/` unless noted):
- Screen base `UIBaseDialogScreen` (`UI/BaseScreens.h:17`): ctor `(const Path &gamePath)`, override `CreateViews()`, `tag()`; `AddStandardBack(parent)`; member `gamePath_`. Template screen: `UI/DisplayLayoutScreen.{h,cpp}`.
- Push: `screenManager()->push(new XxxScreen(...));`. Back: `->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);`.
- Scroll list: `ScrollView(ORIENT_VERTICAL, ...)` containing `LinearLayout(ORIENT_VERTICAL)`; rows `new Choice(label)` / `new ChoiceWithValueDisplay(std::string *value, text, I18NCat, ...)`; `RememberPosition(&float)` to persist scroll.
- Grouping: `CollapsibleSection(std::string_view title, LayoutParams*)`, `SetOpenPtr(bool*)`.
- Slider: `PopupSliderChoiceFloat(float *value, float minValue, float maxValue, float defaultValue, std::string_view text, float step, ScreenManager *sm, std::string_view units="", LayoutParams* = 0)`; `SetLiveUpdate(true)`, `SetHasDropShadow(false)`, `Event OnChange`.
- Search: `ViewSearch search_{};` member; `search_.searchBar = parent->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));` then `search_.ApplySearchFilter(listViewGroup, false)` toggles row visibility by `DescribeText()` match (no rebuild). `SetAlwaysVisibleInSearch(true)` pins headers.
- i18n: `auto gr = GetI18NCategory(I18NCat::GRAPHICS); gr->T("English key");`.
- `RecreateViews()` rebuilds the tree after selection changes.

## File Structure

**New:**
- `Core/Slang/SlangPresetLibrary.{h,cpp}` — device-free scan/index (categories, presets, params, search). Added to all seven build lists.
- `UI/SlangShaderScreen.{h,cpp}` — the browser screen. (`UI/` files auto-compile in the Android/desktop app builds; still add to `CMakeLists.txt`'s UI target + the vcxproj UI lists — grep `DisplayLayoutScreen.cpp` to find them.)

**Modified:**
- `GPU/Common/Slang/SlangPreset.h` — add `std::string description;` to `SlangParamDesc`.
- `GPU/Common/Slang/SlangpParser.cpp` — retain the `#pragma parameter` "Description" into `description`.
- `GPU/Common/Slang/SlangFilterChain.{h,cpp}` — accept optional runtime param overrides and use them in `Run`'s `UserParameter` binding.
- `GPU/Common/FramebufferManagerCommon.cpp` — pass `g_Config.mSlangParams` (filtered to the active preset) into the chain each frame.
- `Core/Config.{h,cpp}` — add `std::map<std::string,float> mSlangParams;` + manual `[SlangParams]` Load/Save.
- `UI/GameSettingsScreen.cpp` — replace the Phase 3 "Import" button with a row that pushes `SlangShaderScreen`.
- `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp` — tests for description parsing + library indexing.

## Task Overview (implement in order)

- **Task 1** — Add `description` to `SlangParamDesc`; parser retains it (+ test).
- **Task 2** — `SlangPresetLibrary`: scan `GetSlangShaderDir()`, group by category, list presets (+ test).
- **Task 3** — Library: enumerate a preset's parameters device-free (+ test).
- **Task 4** — `mSlangParams` config map + manual `[SlangParams]` persistence.
- **Task 5** — Wire runtime param overrides into `SlangFilterChain::Run` + `FramebufferManagerCommon` (+ test).
- **Task 6** — `SlangShaderScreen`: category → preset browser + None + activate (writes `sSlangShaderPreset`, clears legacy post-shaders).
- **Task 7** — `SlangShaderScreen`: per-preset parameter sliders bound to `mSlangParams`.
- **Task 8** — Search box + Import/Update button; replace the Graphics-settings entry point.

Tasks 1–5 are device-free and unit-testable (data model, config, render wiring); Tasks 6–8 are the UI, verified by APK build + on-device. Each ends at an independently testable deliverable.

---

### Task 1: Add a description to `SlangParamDesc` and retain it in the parser

**Files:**
- Modify: `GPU/Common/Slang/SlangPreset.h`
- Modify: `GPU/Common/Slang/SlangpParser.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: existing `SlangParamDesc`, `ParseParameterPragma`.
- Produces: `SlangParamDesc` gains `std::string description;` (append after `name`, before `initial`, so existing brace-init/positional uses in tests still compile — verify none use positional aggregate init; they use field assignment, so appending is safe). The `#pragma parameter NAME "Description" ...` label is stored in `description` (falls back to `name` if empty, decided at UI time, not here).

- [ ] **Step 1: Write the failing test** — append `TestSlangParamDescription()` to `TestSlangParser.cpp`; register `TEST_ITEM(SlangParamDescription)`:

```cpp
bool TestSlangParamDescription() {
	SlangSource src; std::string err;
	std::string shader =
		"#version 450\n"
		"#pragma parameter crt_gamma \"CRT Gamma\" 2.4 1.0 4.0 0.05\n"
		"#pragma stage vertex\n"
		"void main() {}\n"
		"#pragma stage fragment\n"
		"void main() {}\n";
	EXPECT_TRUE(SplitSlangSource(shader, &src, &err));
	EXPECT_EQ_INT((int)src.params.size(), 1);
	EXPECT_TRUE(src.params[0].name == "crt_gamma");
	EXPECT_TRUE(src.params[0].description == "CRT Gamma");
	EXPECT_TRUE(src.params[0].initial == 2.4f);
	EXPECT_TRUE(src.params[0].maximum == 4.0f);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — `./build-unittest/PPSSPPUnitTest SlangParamDescription` → compile error (no `description` field).

- [ ] **Step 3: Add the field** in `SlangPreset.h`:
```cpp
struct SlangParamDesc {
	std::string name;         // must match a float UBO/push member
	std::string description;  // human-readable label from #pragma parameter (may be empty)
	float initial = 0.0f;
	float minimum = 0.0f;
	float maximum = 1.0f;
	float step = 0.01f;
};
```

- [ ] **Step 4: Retain the description** in `SlangpParser.cpp`'s `ParseParameterPragma` — it already extracts the quoted string between `q1` and `q2`; store it:
```cpp
	p->name = std::string(StripSpaces(rest.substr(0, q1)));
	p->description = rest.substr(q1 + 1, q2 - q1 - 1);   // text between the quotes
	std::string tail = rest.substr(q2 + 1);  // " INIT MIN MAX [STEP]"
```

- [ ] **Step 5: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangParamDescription` → exit 0. Also run `SlangParser SlangSplit SlangReflection` → all exit 0 (no regression to existing param parsing).

- [ ] **Step 6: Commit**
```bash
git add GPU/Common/Slang/SlangPreset.h GPU/Common/Slang/SlangpParser.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: retain #pragma parameter description for the parameter UI

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `SlangPresetLibrary` — scan the slang dir and group presets by category

**Files:**
- Create: `Core/Slang/SlangPresetLibrary.h`, `Core/Slang/SlangPresetLibrary.cpp`
- Modify: the seven build lists (as in the Global Constraints)
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `GetSlangShaderDir()` (`Core/Slang/SlangPaths.h`); `File::GetFilesInDir` (recursive walk — check the exact helper: PPSSPP has `File::GetFilesInDir(Path, std::vector<FileInfo>*, filter)`; for recursion either recurse manually or use the directory walk pattern from `PostShader.cpp:100`). `Path` ops.
- Produces (`SlangPresetLibrary.h`):
  ```cpp
  #pragma once
  #include <string>
  #include <vector>
  #include "Common/File/Path.h"

  struct SlangPresetEntry {
      std::string category;     // first path segment under the slang root; "" -> "misc"
      std::string displayName;  // .slangp filename without extension
      Path path;                // absolute path to the .slangp
  };

  class SlangPresetLibrary {
  public:
      // Rescan GetSlangShaderDir() (recursively) for .slangp files. Cheap; call after import
      // or on screen open. Device-free (no shader compilation).
      void Rescan();
      const std::vector<std::string> &GetCategories() const { return categories_; }
      // Presets in a category, in stable (sorted) order.
      std::vector<SlangPresetEntry> GetPresets(const std::string &category) const;
      const std::vector<SlangPresetEntry> &All() const { return entries_; }
      bool Empty() const { return entries_.empty(); }
  private:
      std::vector<SlangPresetEntry> entries_;
      std::vector<std::string> categories_;  // unique, sorted
  };
  ```
  Later tasks may make a process-wide instance; for now the screen owns one.

- [ ] **Step 1: Write the failing test** — append `TestSlangPresetLibrary()`; register `TEST_ITEM(SlangPresetLibrary)`. Build a fake tree under a temp dir and point the library at it. Since `Rescan()` uses `GetSlangShaderDir()`, add a test-only overload `Rescan(const Path &root)` (public) that the production `Rescan()` calls with `GetSlangShaderDir()`; the test calls `Rescan(tempRoot)`:

```cpp
bool TestSlangPresetLibrary() {
	Path root("/tmp/slanglib_test");
	File::DeleteDirRecursively(root);
	File::CreateFullPath(root / "crt");
	File::CreateFullPath(root / "handheld");
	File::WriteStringToFile(true, "shaders = 0\n", root / "crt" / "crt-royale.slangp");
	File::WriteStringToFile(true, "shaders = 0\n", root / "crt" / "crt-lottes.slangp");
	File::WriteStringToFile(true, "shaders = 0\n", root / "handheld" / "lcd.slangp");
	File::WriteStringToFile(true, "shaders = 0\n", root / "bilinear.slangp");   // root -> "misc"
	File::WriteStringToFile(true, "not a preset\n", root / "crt" / "readme.txt"); // ignored

	SlangPresetLibrary lib;
	lib.Rescan(root);
	// categories: crt, handheld, misc (sorted), no "readme"
	const auto &cats = lib.GetCategories();
	EXPECT_TRUE(std::find(cats.begin(), cats.end(), "crt") != cats.end());
	EXPECT_TRUE(std::find(cats.begin(), cats.end(), "handheld") != cats.end());
	EXPECT_TRUE(std::find(cats.begin(), cats.end(), "misc") != cats.end());
	// crt has 2 presets, sorted, displayName has no extension
	auto crt = lib.GetPresets("crt");
	EXPECT_EQ_INT((int)crt.size(), 2);
	EXPECT_TRUE(crt[0].displayName == "crt-lottes");   // sorted
	EXPECT_TRUE(crt[1].displayName == "crt-royale");
	EXPECT_TRUE(crt[0].path.GetFileExtension() == ".slangp");
	// only 4 presets total (txt ignored)
	EXPECT_EQ_INT((int)lib.All().size(), 4);
	File::DeleteDirRecursively(root);
	return true;
}
```
Add includes to the test: `<algorithm>`, `Core/Slang/SlangPresetLibrary.h`, `Common/File/FileUtil.h`.

- [ ] **Step 2: Run to verify it fails** — compile error (library undefined).

- [ ] **Step 3: Implement `SlangPresetLibrary.cpp`** (GPL header). `Rescan()` calls `Rescan(GetSlangShaderDir())`. `Rescan(root)`:
  - Clear `entries_`/`categories_`.
  - Recursively walk `root` collecting files whose extension is `.slangp` (case-insensitive). For recursion, use `File::GetFilesInDir(dir, &fileInfos, nullptr)` and recurse into `fileInfo.isDirectory` entries (mirror the walk in `PostShader.cpp`; verify the exact `FileInfo` field names — `isDirectory`, `fullName`/`name` — by reading `Common/File/DirListing.h`).
  - For each `.slangp`: `category` = first path segment of the file's path relative to `root` (if the file is directly in `root`, category = "misc"); `displayName` = filename without `.slangp`; `path` = absolute path.
  - Build `categories_` as the sorted unique set; sort `entries_` by (category, displayName).
  - `GetPresets(category)` returns the sorted subset.

- [ ] **Step 4: Register** `Core/Slang/SlangPresetLibrary.cpp` + `.h` in all seven build lists.

- [ ] **Step 5: Run to verify it passes** — `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest SlangPresetLibrary` → exit 0.

- [ ] **Step 6: Commit**
```bash
git add Core/Slang/SlangPresetLibrary.h Core/Slang/SlangPresetLibrary.cpp CMakeLists.txt Core/Core.vcxproj Core/Core.vcxproj.filters libretro/Makefile.common android/jni/Android.mk UWP/CoreUWP/CoreUWP.vcxproj UWP/CoreUWP/CoreUWP.vcxproj.filters unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: add SlangPresetLibrary category/preset index (device-free)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Library — enumerate a preset's parameters without compiling

**Files:**
- Modify: `Core/Slang/SlangPresetLibrary.h`, `Core/Slang/SlangPresetLibrary.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `ParseSlangPreset`, `ResolveSlangIncludes`, `SplitSlangSource` (all device-free), file reads.
- Produces (add to `SlangPresetLibrary.h`):
  ```cpp
  // Enumerate a preset's #pragma parameters by text-parsing its .slangp + each .slang
  // (with includes resolved). No GPU/compile. Returns the merged list (.slangp-level
  // overrides win over .slang defaults, first-seen wins across passes), or false + *error.
  bool GetPresetParameters(const Path &presetPath, std::vector<SlangParamDesc> *out, std::string *error);
  ```
  (Free function or static method — free function in the SlangPresetLibrary translation unit is fine.)

- [ ] **Step 1: Write the failing test** — append `TestSlangPresetParameters()`; register it. Build a minimal 1-pass preset whose `.slang` declares two `#pragma parameter`s:

```cpp
bool TestSlangPresetParameters() {
	Path root("/tmp/slangparam_test");
	File::DeleteDirRecursively(root);
	File::CreateFullPath(root);
	File::WriteStringToFile(true,
		"shaders = 1\n"
		"shader0 = a.slang\n", root / "p.slangp");
	File::WriteStringToFile(true,
		"#version 450\n"
		"#pragma parameter gamma \"Gamma\" 2.2 1.0 3.0 0.1\n"
		"#pragma parameter bright \"Brightness\" 1.0 0.0 2.0 0.05\n"
		"#pragma stage vertex\nvoid main() {}\n"
		"#pragma stage fragment\nvoid main() {}\n", root / "a.slang");

	std::vector<SlangParamDesc> params; std::string err;
	EXPECT_TRUE(GetPresetParameters(root / "p.slangp", &params, &err));
	EXPECT_EQ_INT((int)params.size(), 2);
	EXPECT_TRUE(params[0].name == "gamma");
	EXPECT_TRUE(params[0].description == "Gamma");
	EXPECT_TRUE(params[1].name == "bright");
	EXPECT_TRUE(params[1].maximum == 2.0f);
	File::DeleteDirRecursively(root);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — compile error (function undefined).

- [ ] **Step 3: Implement `GetPresetParameters`** in `SlangPresetLibrary.cpp`. Mirror the merge logic from `SlangFilterChain::Load` (`SlangFilterChain.cpp:104` + `:240-275`) WITHOUT the compile:
  - Read the `.slangp` text; `ParseSlangPreset(text, presetPath.GetDirectory(), &preset, error)`. Seed `out` with `preset.params` (any `.slangp`-level `#pragma parameter` overrides — usually none).
  - For each `pass.shaderPath`: read the `.slang` file (plain `File::ReadBinaryFileToString`, or reuse the VFS-then-file `ReadSlangFile` pattern; for imported files a plain file read is fine — verify path is absolute from the parser), `ResolveSlangIncludes(src, Path(pass.shaderPath).GetDirectory(), reader, &resolved, error)`, `SplitSlangSource(resolved, &slangSrc, error)`, then merge each `slangSrc.params[i]` into `out` if a param of that name isn't already present (first-seen wins — matches Load).
  - Return true even if `out` is empty (a preset may have zero parameters).

- [ ] **Step 4: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangPresetParameters` → exit 0.

- [ ] **Step 5: Commit**
```bash
git add Core/Slang/SlangPresetLibrary.h Core/Slang/SlangPresetLibrary.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: enumerate a preset's #pragma parameters device-free

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `mSlangParams` config map with manual `[SlangParams]` persistence

**Files:**
- Modify: `Core/Config.h`, `Core/Config.cpp`

**Interfaces:**
- Produces: `std::map<std::string, float> mSlangParams;` on `g_Config`, keyed `"<absolutePresetPath>|<paramName>"`. Persisted in an ini section `[SlangParams]`. Not registered via `ConfigSetting` (maps aren't supported) — manual Load/Save mirroring `mPostShaderSetting`.
- Rationale for the composite key: one flat map covers all presets; a param is only meaningful per (preset, name), and prefixing with the preset path keeps different presets' identically-named params (e.g. `gamma`) independent.

- [ ] **Step 1: Declare the member** in `Core/Config.h`, immediately after `std::map<std::string, float> mPostShaderSetting;` (near `Config.h:372`):
```cpp
	// Slang shader runtime parameter overrides, keyed "<presetPath>|<paramName>" -> value.
	// Persisted manually in the [SlangParams] ini section (see Config.cpp Load/Save).
	std::map<std::string, float> mSlangParams;
```

- [ ] **Step 2: Add Save** in `Core/Config.cpp`, in the same non-game-specific block right after the `PostShaderSetting` save (near `Config.cpp:1522`):
```cpp
		Section *slangParams = iniFile.GetOrCreateSection("SlangParams");
		slangParams->Clear();
		for (const auto &[k, v] : mSlangParams) {
			slangParams->Set(k.c_str(), v);
		}
```
(Match the exact surrounding indentation/scope. If `mPostShaderSetting` is saved under an `if (!IsGameSpecific())`-style guard, place this in the same guard so slang params are global, not per-game — consistent with `sSlangShaderPreset` being the only per-game slang key.)

- [ ] **Step 3: Add Load** in `Core/Config.cpp`, right after the `mPostShaderSetting` load (near `Config.cpp:1414`):
```cpp
	// Load slang shader runtime parameter overrides.
	const Section *slangParams = iniFile.GetOrCreateSection("SlangParams");
	mSlangParams.clear();
	for (const auto &[key, value] : slangParams->ToMap()) {
		mSlangParams[key] = std::stof(value);
	}
```
Guard the `std::stof` against malformed values (a hand-edited ini could contain non-numbers): wrap in try/catch or check with a helper, logging and skipping a bad entry rather than throwing. (Verify how `mPostShaderSetting`'s `std::stof` handles this today; match or slightly harden.)

- [ ] **Step 4: Build to verify** — `cmake --build build-unittest --target PPSSPPUnitTest` → success. No dedicated unit test (config (de)serialization has no existing test harness seam here); correctness is exercised by Task 7 on-device (values persist across a settings round-trip). If a quick guard against `std::stof` throwing is added, that's the only logic worth an inline sanity check.

- [ ] **Step 5: Commit**
```bash
git add Core/Config.h Core/Config.cpp
git commit -m "slang: persist runtime parameter overrides in [SlangParams] ini section

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Feed runtime parameter overrides into `SlangFilterChain::Run`

**Files:**
- Modify: `GPU/Common/Slang/SlangFilterChain.h`, `GPU/Common/Slang/SlangFilterChain.cpp`
- Modify: `GPU/Common/FramebufferManagerCommon.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `g_Config.mSlangParams`, the active `g_Config.sSlangShaderPreset`.
- Produces: `SlangFilterChain` accepts an optional per-frame param-override map and uses it in `Run`'s `UserParameter` binding instead of the compiled-in default. Signature: add a setter (cleaner than threading a map through `Run`'s already-long arg list):
  ```cpp
  // Runtime overrides for #pragma parameter values: name -> value. Applied in Run()'s
  // UserParameter binding; a name absent from the map falls back to the parameter's default.
  void SetParamOverrides(const std::map<std::string, float> &overrides);
  ```
  with a private `std::map<std::string, float> paramOverrides_;` member.

- [ ] **Step 1: Write the failing test** — this is testable device-free because it exercises the value-selection logic, not the GPU. Refactor the override lookup into a tiny pure helper and test it:
  Add to `SlangFilterChain.h` a `static float ResolveParamValue(const std::string &name, const std::vector<SlangParamDesc> &params, const std::map<std::string, float> &overrides);` (declared static, defined in the .cpp), then test:

```cpp
bool TestSlangParamOverride() {
	std::vector<SlangParamDesc> params;
	SlangParamDesc g; g.name = "gamma"; g.initial = 2.2f; params.push_back(g);
	SlangParamDesc b; b.name = "bright"; b.initial = 1.0f; params.push_back(b);
	std::map<std::string, float> ov; ov["gamma"] = 2.8f;   // override gamma only
	// gamma overridden, bright falls back to default, unknown falls back to 0
	EXPECT_TRUE(SlangFilterChain::ResolveParamValue("gamma", params, ov) == 2.8f);
	EXPECT_TRUE(SlangFilterChain::ResolveParamValue("bright", params, ov) == 1.0f);
	EXPECT_TRUE(SlangFilterChain::ResolveParamValue("nope", params, ov) == 0.0f);
	return true;
}
```
Register `TEST_ITEM(SlangParamOverride)`.

- [ ] **Step 2: Run to verify it fails** — compile error (`ResolveParamValue` undefined).

- [ ] **Step 3: Implement.** In `SlangFilterChain.cpp`, add:
```cpp
float SlangFilterChain::ResolveParamValue(const std::string &name,
		const std::vector<SlangParamDesc> &params, const std::map<std::string, float> &overrides) {
	auto it = overrides.find(name);
	if (it != overrides.end()) return it->second;
	for (const auto &p : params) if (p.name == name) return p.initial;
	return 0.0f;
}
```
Add `void SlangFilterChain::SetParamOverrides(const std::map<std::string, float> &o) { paramOverrides_ = o; }` and the `paramOverrides_` member. In `Run`'s `UserParameter` case (`SlangFilterChain.cpp:559-566`), replace the inline default-value loop with:
```cpp
			case SlangSemantic::UserParameter: {
				float val = ResolveParamValue(m.name, preset_.params, paramOverrides_);
				memcpy(dst, &val, std::min((size_t)4, avail));
				break;
			}
```

- [ ] **Step 4: Wire the map in `FramebufferManagerCommon.cpp`.** Where the slang chain is run each frame (the `slangChain_->Run(...)` call, ~`FramebufferManagerCommon.cpp:1773`), first push the active preset's overrides. Build a filtered map from `g_Config.mSlangParams` (only keys prefixed with `sSlangShaderPreset + "|"`, stripped to the bare param name) and call `slangChain_->SetParamOverrides(filtered)` before `Run`. Do this in `UpdateSlangChain` (once per reload) if the map only changes on user edit — but since sliders can change it live, set it each frame before Run (cheap: a small map copy). Keep it simple: build the filtered map in the present path and call `SetParamOverrides` right before `Run`.
  Example (near the existing `slangChain_->Run`):
```cpp
	std::map<std::string, float> slangOverrides;
	const std::string prefix = g_Config.sSlangShaderPreset + "|";
	for (const auto &[k, v] : g_Config.mSlangParams) {
		if (k.compare(0, prefix.size(), prefix) == 0)
			slangOverrides[k.substr(prefix.size())] = v;
	}
	slangChain_->SetParamOverrides(slangOverrides);
```

- [ ] **Step 5: Run to verify it passes** — `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest SlangParamOverride` → exit 0. Run `SlangParser SlangReflection SlangPushConstant` → all exit 0. Then build the APK and confirm crt-royale still renders with an empty override map (no behavior change: `ResolveParamValue` returns `p.initial` exactly as before).

- [ ] **Step 6: Commit**
```bash
git add GPU/Common/Slang/SlangFilterChain.h GPU/Common/Slang/SlangFilterChain.cpp GPU/Common/FramebufferManagerCommon.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: apply runtime parameter overrides in the filter chain

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: `SlangShaderScreen` — category → preset browser with activate + None

**Files:**
- Create: `UI/SlangShaderScreen.h`, `UI/SlangShaderScreen.cpp`
- Modify: `CMakeLists.txt` + the vcxproj UI source lists (grep for `UI/DisplayLayoutScreen.cpp` to find every list that references UI screens; add the new files alongside).

**Interfaces:**
- Consumes: `SlangPresetLibrary` (Task 2/3), `g_Config.sSlangShaderPreset`, `g_Config.vPostShaderNames`, `UIBaseDialogScreen`, `ScrollView`/`LinearLayout`/`Choice`/`CollapsibleSection`, `I18NCat::GRAPHICS`.
- Produces (`SlangShaderScreen.h`):
  ```cpp
  #pragma once
  #include "UI/BaseScreens.h"
  #include "Core/Slang/SlangPresetLibrary.h"

  class SlangShaderScreen : public UIBaseDialogScreen {
  public:
      explicit SlangShaderScreen(const Path &gamePath) : UIBaseDialogScreen(gamePath) {}
      void CreateViews() override;
      const char *tag() const override { return "SlangShader"; }
  private:
      void ActivatePreset(const Path &presetPath);   // sets sSlangShaderPreset, clears legacy, RecreateViews
      void Deactivate();                              // sSlangShaderPreset = "", RecreateViews
      SlangPresetLibrary library_;
  };
  ```

- [ ] **Step 1: Implement `CreateViews`** (GPL header). Structure (mirror `DisplayLayoutScreen::CreateViews` layout scaffolding + `CwCheatScreen` scroll list):
  - `library_.Rescan();` at the top.
  - Root: a vertical `LinearLayout` (or the standard two-column `ViewGroup` from the template) inside the screen; `AddStandardBack(...)` for the back button.
  - Title via `gr->T("RetroArch (slang) shaders")`.
  - If `library_.Empty()`: show a notice row `gr->T("No slang shaders imported yet")` and (Task 8) the Import button. Return early after adding the import affordance.
  - A `ScrollView(ORIENT_VERTICAL)` containing a vertical `LinearLayout` (`listContainer`). `RememberPosition(&g_Config.fGameListScrollPosition)`? No — add a NEW `float` config or reuse none; simplest: don't persist scroll for v1 (omit `RememberPosition`).
  - First row: a `Choice(gr->T("None (disable)"))` whose handler calls `Deactivate()`. Mark it selected/highlighted if `sSlangShaderPreset` is empty (append " ✓" to the label, or set a distinct style — keep it simple: append a check mark to the active entry's label).
  - For each category in `library_.GetCategories()`: add a `CollapsibleSection(category)` (or a plain `ItemHeader(category)` for v1 simplicity), then for each `SlangPresetEntry` from `library_.GetPresets(category)` add a `Choice(entry.displayName)` whose handler calls `ActivatePreset(entry.path)`. Append a check mark to the entry whose `entry.path.ToString() == g_Config.sSlangShaderPreset`.

- [ ] **Step 2: Implement `ActivatePreset`**:
```cpp
void SlangShaderScreen::ActivatePreset(const Path &presetPath) {
	g_Config.sSlangShaderPreset = presetPath.ToString();
	// Mutual exclusivity: a slang preset and the legacy post-shader chain never both run.
	g_Config.vPostShaderNames.clear();
	g_Config.vPostShaderNames.push_back("Off");
	RecreateViews();   // refresh the active-entry check mark
}
```
`Deactivate()` sets `g_Config.sSlangShaderPreset.clear();` then `RecreateViews();`. (The renderer picks up the change next frame via `UpdateSlangChain`'s path-compare; no explicit reload call needed. Verify `UpdateSlangChain` is invoked on config change — it runs in the present path each frame, so setting the config is sufficient.)

- [ ] **Step 3: Register the files** in `CMakeLists.txt` (UI target) and the vcxproj UI lists.

- [ ] **Step 4: Build the APK** — `./gradlew -p android assembleNormalDebug ...` → BUILD SUCCESSFUL. (UI doesn't compile in the unittest target; the APK build is the compile gate. Watch for `OnClick.Add` vs `.Handle` idiom mistakes — handlers are `void` lambdas taking `UI::EventParams &`; there is no `UI::EVENT_DONE` return for `.Add`.)

- [ ] **Step 5: On-device check (controller)** — temporarily reachable only after Task 8 wires the entry point; for this task, verify compile + that the screen can be pushed from a scratch call site if convenient, else defer the visual check to Task 8. Note this in the report.

- [ ] **Step 6: Commit**
```bash
git add UI/SlangShaderScreen.h UI/SlangShaderScreen.cpp CMakeLists.txt <vcxproj UI lists>
git commit -m "slang: add SlangShaderScreen category/preset browser with activate

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Per-preset parameter sliders bound to `mSlangParams`

**Files:**
- Modify: `UI/SlangShaderScreen.h`, `UI/SlangShaderScreen.cpp`

**Interfaces:**
- Consumes: `GetPresetParameters` (Task 3), `g_Config.mSlangParams` (Task 4), `PopupSliderChoiceFloat`.
- Produces: when a preset is active, its parameters render as live sliders below the browser; changing a slider writes `g_Config.mSlangParams["<presetPath>|<name>"]` and the render path (Task 5) picks it up next frame. Add a "Reset to defaults" `Choice` that erases this preset's entries from `mSlangParams`.

- [ ] **Step 1: Implement the slider section** in `CreateViews`, after the browser list, only when `!g_Config.sSlangShaderPreset.empty()`:
```cpp
	std::vector<SlangParamDesc> params; std::string err;
	if (GetPresetParameters(Path(g_Config.sSlangShaderPreset), &params, &err) && !params.empty()) {
		listContainer->Add(new ItemHeader(gr->T("Shader parameters")));
		const std::string prefix = g_Config.sSlangShaderPreset + "|";
		for (const auto &p : params) {
			const std::string key = prefix + p.name;
			bool existed = g_Config.mSlangParams.find(key) != g_Config.mSlangParams.end();
			float &value = g_Config.mSlangParams[key];   // map auto-creates
			if (!existed) value = p.initial;             // seed with the shader default
			const std::string label = p.description.empty() ? p.name : p.description;
			float step = p.step > 0.0f ? p.step : (p.maximum - p.minimum) / 100.0f;
			PopupSliderChoiceFloat *slider = listContainer->Add(new PopupSliderChoiceFloat(
				&value, p.minimum, p.maximum, p.initial, label, step, screenManager()));
			slider->SetLiveUpdate(true);
			slider->SetHasDropShadow(false);
		}
		listContainer->Add(new Choice(gr->T("Reset parameters to defaults")))->OnClick.Add(
			[this](UI::EventParams &e) {
				const std::string pfx = g_Config.sSlangShaderPreset + "|";
				for (auto it = g_Config.mSlangParams.begin(); it != g_Config.mSlangParams.end(); ) {
					if (it->first.compare(0, pfx.size(), pfx) == 0) it = g_Config.mSlangParams.erase(it);
					else ++it;
				}
				RecreateViews();
			});
	}
```
Note: the slider binds `&value` directly into the persistent `mSlangParams` entry (same pattern as `mPostShaderSetting` sliders at `DisplayLayoutScreen.cpp:471-503`), so edits persist to config on the next Save and reach the renderer live via Task 5.

- [ ] **Step 2: Build the APK** → BUILD SUCCESSFUL.

- [ ] **Step 3: On-device check (controller)** — deferred to Task 8 (needs the entry point). Note in report.

- [ ] **Step 4: Commit**
```bash
git add UI/SlangShaderScreen.h UI/SlangShaderScreen.cpp
git commit -m "slang: per-preset parameter sliders bound to persistent overrides

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Search box, Import/Update button, and Graphics-settings entry point

**Files:**
- Modify: `UI/SlangShaderScreen.h`, `UI/SlangShaderScreen.cpp`
- Modify: `UI/GameSettingsScreen.cpp`

**Interfaces:**
- Consumes: `ViewSearch`/`SearchBar` (`UI/MiscViews.h`), `g_SlangImporter` (Phase 3), `PopupSliderChoiceFloat`.
- Produces: the screen gains a search filter over the preset list and an Import/Update button; the Graphics-settings row now pushes `SlangShaderScreen` instead of directly starting the importer.

- [ ] **Step 1: Add the Import/Update button** at the top of `SlangShaderScreen::CreateViews`. Reuse the exact Phase 3 logic (guard `g_SlangImporter.Busy()`, call `Start("")`, show an OSD error if `Start` returns false — copy from `GameSettingsScreen.cpp`). Label it `gr->T("Import / update shaders")` when the library is non-empty, `gr->T("Import RetroArch (slang) shaders")` when empty. The OSD progress/completion still comes from the NativeApp pump added in Phase 3; after a completed import the user reopens/refreshes the screen (add a note; optionally call `library_.Rescan()` + `RecreateViews()` from `update()` when `g_SlangImporter.GetState()` transitions to DONE — keep v1 simple: rescan on each `CreateViews`, so reopening the screen shows new shaders).

- [ ] **Step 2: Add the search box.** Add `ViewSearch search_{};` member. In `CreateViews`, add `search_.searchBar = <container>->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));` above the scroll list, and after building the list call `search_.ApplySearchFilter(listContainer, false);`. Wire `search_.searchBar->OnCancel` to clear the filter and re-apply (copy the `CwCheatScreen.cpp:266-270` wiring). Pin category headers with `SetAlwaysVisibleInSearch(true)` so filtering hides only preset rows. Forward key input: override the screen's key handler to call `search_.Key(listContainer, input)` if the template requires it (check how `CwCheatScreen` forwards keys; replicate).

- [ ] **Step 3: Replace the Graphics-settings entry point** in `UI/GameSettingsScreen.cpp`. Find the Phase 3 row (the `Choice(gr->T("Import RetroArch (slang) shaders"))` under the Display section that calls `g_SlangImporter.Start`). Replace its handler to push the screen instead:
```cpp
		Choice *slangChoice = graphicsSettings->Add(new Choice(gr->T("RetroArch (slang) shaders")));
		slangChoice->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->push(new SlangShaderScreen(gamePath_));
		});
```
Add `#include "UI/SlangShaderScreen.h"` to `GameSettingsScreen.cpp`. Remove the now-unused direct-import lambda (the import lives inside the screen now). Keep the NativeApp OSD pump (Phase 3) — it is independent of where import is triggered.

- [ ] **Step 4: Build the APK** → BUILD SUCCESSFUL.

- [ ] **Step 5: On-device end-to-end (controller).** Verify: (a) Graphics → "RetroArch (slang) shaders" opens the screen; (b) Import populates the list (or list already present from Phase 3 import); (c) categories show, search filters presets; (d) selecting crt-royale sets it active (check mark) and the game renders with crt-royale; (e) its parameter sliders appear and moving one visibly changes the image; (f) "Reset parameters to defaults" restores; (g) reopening the app preserves the selected preset and tweaked params (config round-trip); (h) "None" disables slang and the game renders raw. Record each in the commit body.

- [ ] **Step 6: Commit**
```bash
git add UI/SlangShaderScreen.h UI/SlangShaderScreen.cpp UI/GameSettingsScreen.cpp
git commit -m "slang: add search + import to shader screen; wire Graphics entry point

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Out of scope for Phase 4 (per design doc §9 / deferred)

- **Backend expansion** (D3D11, OpenGL/GLES) — **Phase 5**. Slang remains Vulkan-only; the screen should still list/select on any backend (selection is backend-agnostic; the chain only runs under Vulkan today — if a non-Vulkan backend is active, selection has no visual effect, matching current Phase 1-3 behavior. Optionally show a one-line "Vulkan only" note; not required).
- **Per-preset "update available" detection** beyond the single manifest; mirror-list download UI.
- **Thumbnails / previews** of shaders.
- Disk-cached compiled SPIR-V.

## Self-Review (completed during authoring)

- **Design §10 coverage:** SlangPresetLibrary with GetCategories/GetPresets/search (Task 2/3/8); SlangShaderScreen two-level browser with Import button, search, category→preset, None entry, active indicator, parameter sliders (Tasks 6-8); mutual exclusivity with legacy post-shaders (Task 6); config keys sSlangShaderPreset (existing) + mSlangParams (Task 4); Graphics entry point (Task 8). The design's "SlangPresetLibrary rebuilt on import completion" is satisfied by rescanning on CreateViews (Task 8 Step 1).
- **Cross-task type consistency:** `SlangPresetEntry{category,displayName,path}`, `GetPresetParameters(Path,&vector<SlangParamDesc>,&string)`, `SetParamOverrides`/`ResolveParamValue`, and the `"<presetPath>|<paramName>"` key format are used identically across Tasks 2→8. `SlangParamDesc.description` (Task 1) is consumed by Task 7's slider label.
- **Phase 3 carryover honored:** the library scans `GetSlangShaderDir()` (Global Constraints), not `DIRECTORY_CUSTOM_SHADERS` — the exact issue flagged in the Phase 3 ledger.
- **Regression safety:** empty `mSlangParams` → `ResolveParamValue` returns `p.initial`, identical to today's render (Task 5 Step 5 verifies crt-royale unchanged). Legacy post-shader config/UI untouched.
- **Placeholder scan:** every code step carries concrete code; two spots explicitly say "verify the exact API in-file before writing" (recursive dir-walk `FileInfo` field names in Task 2; key-forwarding for search in Task 8) — these are real "read the neighbor first" instructions, not vague TODOs, because they depend on signatures the implementer must confirm at that moment.
- **Build-list discipline:** Task 2 (Core file) updates all seven lists; Tasks 6/8 (UI files) update the UI lists — both call out the grep to find them, matching how Phase 3 handled it.

