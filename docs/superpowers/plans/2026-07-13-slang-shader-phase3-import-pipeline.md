# Slang Shader Support — Phase 3 (Import Pipeline) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user download the libretro slang-shaders package from the buildbot, unpack it safely, and import it into the PPSSPP custom-shaders directory — with a minimal in-app trigger — so the Phase 1/2 rendering core can load any preset from the imported tree without a manual file copy.

**Architecture:** Add a new **network/file-only** component `Core/Slang/SlangPackageImporter.{h,cpp}` that mirrors the existing `GameManager` download→extract→install lifecycle (poll-driven off `g_DownloadManager.Update()`, extraction on a worker thread). It downloads `shaders_slang.zip` to a temp file, extracts **only** shader-relevant files into a temp directory with explicit zip-slip protection, writes a `manifest.json`, then atomically swaps the temp dir into `GetSysDirectory(DIRECTORY_CUSTOM_SHADERS)/slang/`. Two new config keys (`sSlangBuildbotUrl`, and reuse of existing `sSlangShaderPreset`) are added. Before any of that, this phase **hardens the Phase 1/2 rendering core against malformed/malicious shader files** (three confirmed code-review findings), because Phase 3 is the first time PPSSPP loads *unverified downloaded* slang content. No GPU code is touched by the importer itself.

**Tech Stack:** C++17; PPSSPP `http::RequestManager` (`g_DownloadManager`), libzip via `zip_t`/`zip_fopen_index`/`zip_fread` (as `GameManager::ExtractFile` uses), `Common/File/FileUtil.h` (`File::` free functions), `Common/File/Path.h`, `Common/Data/Format/JSONWriter`/`JSONReader`, `Core/Util/PathUtil.h` (`GetSysDirectory`), PPSSPP `unittest` harness. No new third-party deps.

## Global Constraints

- **License header:** every new/modified file keeps the PPSSPP GPL 2.0 header. New Slang files use the year `2026-` (copy the style from existing `GPU/Common/Slang/*` files).
- **Zero GPU-path regression:** the importer is network/file only. The hardening tasks (Task 1–3) must not change rendering output for well-formed shaders; the existing slang unit tests and on-device crt-royale render must stay identical.
- **Untrusted input is the threat model:** every byte the importer handles comes from a remote zip. All extraction must reject path traversal (`..`, absolute paths, drive-relative) and only write files under the destination root. The rendering-core hardening (Task 1–3) assumes shader files may be adversarial.
- **Mirror existing precedent, do not reinvent:** the download/extract lifecycle must follow `Core/Util/GameManager.cpp` (`DownloadAndInstall` → `Update` → `InstallZipOnThread` → `ExtractZipContents`) — poll-based, one download at a time, worker-thread extraction, `File::` for all filesystem ops. Do not add a second HTTP or zip abstraction.
- **Atomicity:** extract into a temp directory, then swap into place with `File::Move`; on any failure, `File::DeleteDirRecursively` the temp dir and leave any existing install untouched.
- **Coexistence:** `sSlangShaderPreset` (existing, `Core/Config.h:379-380`, `Core/Config.cpp:734`) is the only key that selects an active preset; the importer only lays down files and never changes the active preset. Legacy post-shader config is untouched.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Build + test env (from Phase 2, still valid):**
  - Desktop unit build: `cmake --build build-unittest --target PPSSPPUnitTest` (reconfigure per Phase 2 plan if `build-unittest/` is absent).
  - Unit tests are `bool TestXxx()` using `unittest/UnitTest.h` macros, registered in `unittest/UnitTest.cpp` (forward decl + `TEST_ITEM`). Run `./build-unittest/PPSSPPUnitTest <Name>`; **exit 0 = pass** (no success banner).
  - On-device (for the hardening tasks' render check only): Android arm64 APK via `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=slang-dev --console=plain`; install `adb install -t -r <apk>`. Device: Adreno/Vulkan. crt-royale preset at `/storage/9C33-6BBD/ROMs/psp/PSP/shaders/crt/crt-royale.slangp`.

## Verbatim reference: existing infrastructure this plan builds on

**Download** (`Common/Net/HTTPRequest.h`; global `g_DownloadManager` declared `Core/Config.h:802`):
```cpp
// RequestFlags: Default=0, ProgressBar=1, ProgressBarDelayed=2, Cached24H=4, KeepInMemory=8
std::shared_ptr<http::Request> RequestManager::StartDownload(
    std::string_view url, const Path &outfile, http::RequestFlags flags,
    const char *acceptMime = nullptr, std::string_view name = "",
    http::RequestCompletionCallback completionCallback = {});
// On a std::shared_ptr<http::Request>:
bool Done();  bool Failed() const;  float Progress() const;  int ResultCode() const;
const Path &OutFile() const;  void Cancel();
// Must pump on UI thread each frame:  g_DownloadManager.Update();
```

**GameManager lifecycle to mirror** (`Core/Util/GameManager.{h,cpp}`):
- `GameManagerState { IDLE, DOWNLOADING, INSTALLING }` (`GameManager.h:31`).
- `DownloadAndInstall(url)` (`GameManager.cpp:79`): guards single download, `Path filename = GetTempFilename();` then `curDownload_ = g_DownloadManager.StartDownload(url, filename, http::RequestFlags::ProgressBar, acceptMime);`.
- `Update()` (`GameManager.cpp:144`): when `curDownload_->Done()`, checks `ResultCode()==200` && `File::Exists(fileName)`, builds task, calls `InstallZipOnThread(task)`; drops `curDownload_`.
- `InstallZipOnThread(task)` (`GameManager.cpp:824`): `installThread_ = std::thread([this,task](){ InstallZipContents(task); });`. `InstallInProgress()` = `installThread_.joinable()`.
- `ExtractFile(zip *z, int idx, const Path &out, size_t *bytesCopied, size_t allBytes)` (`GameManager.cpp:564`): `zip_stat_index`, `zip_fopen_index`, `zip_fread` in 128 KB blocks, write via `File::OpenCFile(out, "wb")`.
- Temp file: `GetTempFilename()` → `<memstick>/ppsspp.dl` (non-Windows) (`GameManager.cpp:62`).
- **No zip-slip protection exists** anywhere in GameManager extraction — Phase 3 must add its own.

**Zip open** (`Core/Loaders.h:204-205`): `zip_t *ZipOpenPath(const Path &);  void ZipClose(zip_t *);`. libzip entry count: `zip_int64_t zip_get_num_entries(zip_t *, zip_flags_t)`; entry name: `const char *zip_get_name(zip_t *, zip_uint64_t, zip_flags_t)`.

**Directories** (`Core/Util/PathUtil.h`): `Path GetSysDirectory(PSPDirectories)`; `DIRECTORY_CUSTOM_SHADERS` already exists (resolves to `<memstick>/PSP/shaders`).

**Filesystem** (`Common/File/FileUtil.h`, namespace `File`): `Exists`, `CreateFullPath`, `Delete`, `DeleteDirRecursively`, `Move`, `Rename`, `OpenCFile`, `WriteStringToFile(bool textFile, std::string_view, const Path&)`. **Path** (`Common/File/Path.h`): `operator/`, `GetFilename`, `GetFileExtension`, `GetDirectory`, `ToString`, `StartsWith(const Path&)`, `IsAbsolute`, `empty`.

**JSON** (`Common/Data/Format/JSONWriter.h` / `JSONReader.h`, namespace `json`): `JsonWriter(PRETTY)` → `begin()/writeString()/writeInt()/end()/str()`; `JsonReader(filename)` → `ok()`, `root().getString(name,&out)`, `getInt(name,def)`.

**Config** (`Core/Config.{h,cpp}`, macro `SETTING(a,x)` = `&a,&a.x` at `Config.cpp:226`): existing `ConfigSetting("SlangShaderPreset", SETTING(g_Config, sSlangShaderPreset), "", CfgFlag::PER_GAME)` at `Config.cpp:734`.

---

## Task Overview (implement in order)

- **Task 1** — Harden `MatchIndexedName` against integer overflow (review finding #2).
- **Task 2** — Bound texture slot index in `SlangFilterChain::Run` (review finding #3).
- **Task 3** — Make `FindMatchingBrace` skip comments/strings (review finding #1).
- **Task 4** — `sSlangBuildbotUrl` config key + `slang/` path helper.
- **Task 5** — Zip-slip-safe path sanitizer (pure function + unit tests).
- **Task 6** — `SlangPackageImporter`: extraction-to-temp + manifest + atomic swap (thread body, unit-testable with a local zip).
- **Task 7** — `SlangPackageImporter`: download lifecycle + poll API (mirrors GameManager).
- **Task 8** — Minimal UI trigger + `Update()` pump wiring.

Tasks 1–3 are self-contained hardening with unit tests and no interface changes; they gate the untrusted-input work. Tasks 4–8 build the importer bottom-up (config → sanitizer → extractor → download → UI). Each ends at an independently testable deliverable.

---

### Task 1: Harden `MatchIndexedName` against integer overflow

**Files:**
- Modify: `GPU/Common/Slang/SlangReflection.cpp:23-35`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: existing `static bool MatchIndexedName(const std::string &name, const std::string &prefix, int *outIndex)`.
- Produces: same signature; now returns `false` (name unclassified → treated as an unsupported member, which `ReflectSlangSource` already rejects loudly) when the trailing number overflows or exceeds a sane cap.

- [ ] **Step 1: Write the failing test** — append `TestSlangReflectionIndexOverflow()` to `TestSlangParser.cpp`; register `TEST_ITEM(SlangReflectionIndexOverflow)` in `UnitTest.cpp`. `MatchIndexedName` is file-static, so drive it through the public `ClassifyUniform` (a `PassOutputSize<huge>` name must not classify to a valid index):

```cpp
bool TestSlangReflectionIndexOverflow() {
	SlangClassifyContext ctx;  // no params/aliases/luts needed
	int idx = -999;
	// Normal small index still works:
	EXPECT_TRUE(ClassifyUniform("PassOutputSize3", ctx, &idx) == SlangSemantic::PassOutputSize);
	EXPECT_EQ_INT(idx, 3);
	// Overflowing index must NOT be accepted as a valid PassOutputSize:
	idx = -999;
	SlangSemantic s = ClassifyUniform("PassOutputSize999999999999", ctx, &idx);
	EXPECT_TRUE(s != SlangSemantic::PassOutputSize);   // rejected -> Unknown/unclassified
	// Index just over the cap is rejected too:
	idx = -999;
	EXPECT_TRUE(ClassifyUniform("PassOutputSize100000", ctx, &idx) != SlangSemantic::PassOutputSize);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — `./build-unittest/PPSSPPUnitTest SlangReflectionIndexOverflow` → FAIL (current code overflows and returns a wrapped index, classifying as `PassOutputSize`).

- [ ] **Step 3: Implement the guard** in `MatchIndexedName` (`SlangReflection.cpp`). Add `#include <climits>` at the top if not present. Replace the accumulation loop:

```cpp
	int idx = 0;
	for (; pos < name.size(); ++pos) {
		if (!std::isdigit((unsigned char)name[pos])) return false;
		int digit = name[pos] - '0';
		// Reject overflow and absurd indices from untrusted shader files. No real slang
		// shader references >255 passes/history frames; cap well below that ceiling.
		if (idx > (INT_MAX - digit) / 10) return false;
		idx = idx * 10 + digit;
		if (idx > 4096) return false;
	}
	*outIndex = idx;
	return true;
```

- [ ] **Step 4: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangReflectionIndexOverflow` → exit 0. Also run `SlangReflection SlangSemantics SlangSemanticsPhase2` → all exit 0 (no regression to normal indices).

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangReflection.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: reject overflowing/absurd indexed uniform names

MatchIndexedName accumulated trailing digits with no overflow guard, so a
crafted name like PassOutputSize999999999999 wrapped to a negative/small
index that later drove framebuffer/history array indexing. Cap the index
and bail on overflow so the name stays unclassified and ReflectSlangSource
rejects it loudly.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Bound texture slot index in `SlangFilterChain::Run`

**Files:**
- Modify: `GPU/Common/Slang/SlangFilterChain.cpp:658-663`

**Interfaces:**
- Consumes: `MAX_TEXTURE_SLOTS` (already visible via `Common/GPU/thin3d.h`, value 12); `pass.reflection.textures` where each `SlangTextureBinding.binding` comes from SPIR-V reflection of an untrusted shader.
- Produces: no signature change; a binding whose derived slot is out of range is skipped (not bound) with a one-time error log, instead of calling `draw_->BindTexture`/`BindFramebufferAsTexture` with an out-of-range slot.

**Note:** this is a runtime-render change with no unit-test seam (needs a live `Draw::` device). Verify by (a) confirming existing slang unit tests still pass, and (b) on-device crt-royale still renders (all its bindings are in range, so behavior is unchanged for valid shaders). The guard only affects malformed shaders.

- [ ] **Step 1: Add the bounds check** in the texture-binding loop, immediately after the existing `slot < 0` guard. Locate:

```cpp
		for (const auto &tex : pass.reflection.textures) {
			int slot = tex.binding - 1;
			if (slot < 0) {
				continue;  // binding 0 is the UBO, not a texture slot.
			}
```

and replace with:

```cpp
		for (const auto &tex : pass.reflection.textures) {
			int slot = tex.binding - 1;
			if (slot < 0) {
				continue;  // binding 0 is the UBO, not a texture slot.
			}
			if (slot >= (int)MAX_TEXTURE_SLOTS) {
				// A malformed/hostile shader can declare a sampler at an arbitrarily high
				// binding; passing slot >= MAX_TEXTURE_SLOTS to thin3d would index past the
				// backend's bound-texture arrays. Skip it (leaves the sampler unfed, which
				// glslang/reflection already tolerates for unused-in-practice bindings).
				static bool logged = false;
				if (!logged) {
					ERROR_LOG(Log::G3D, "SlangFilterChain: texture '%s' binding %d (slot %d) exceeds MAX_TEXTURE_SLOTS %u; skipping",
						tex.name.c_str(), tex.binding, slot, (unsigned)MAX_TEXTURE_SLOTS);
					logged = true;
				}
				continue;
			}
```

- [ ] **Step 2: Build the unit-test target** to confirm it still compiles — `cmake --build build-unittest --target PPSSPPUnitTest` → success. Run all slang tests (`SlangParser SlangSplit SlangResolution SlangSemantics SlangReflection SlangParserPhase2Keys SlangParserLuts SlangFormatPragma SlangSemanticsPhase2 SlangIncludes SlangPushConstant`) → all exit 0.

- [ ] **Step 3: On-device render check** — build+install the APK, launch with the crt-royale preset active, confirm it still renders correctly (no regression; crt-royale's max slot is well within 12). Record the confirmation in the commit body.

- [ ] **Step 4: Commit**

```bash
git add GPU/Common/Slang/SlangFilterChain.cpp
git commit -m "slang: skip texture bindings past MAX_TEXTURE_SLOTS

Run only rejected slot < 0; a shader declaring a sampler at a high binding
(e.g. binding=100) produced slot=99 and passed it straight to thin3d's
BindTexture/BindFramebufferAsTexture, indexing past the backend's
bound-texture arrays. Skip out-of-range slots with a one-time log. Verified
crt-royale still renders on-device (all bindings in range).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Make `FindMatchingBrace` skip comments and string literals

**Files:**
- Modify: `GPU/Common/Slang/SlangPassCompiler.cpp:70-82`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: existing `static size_t FindMatchingBrace(const std::string &src, size_t start)` used by `TransformPushConstantToUBO`.
- Produces: same signature; braces inside `//` line comments, `/* */` block comments, and `"..."` string literals no longer affect depth counting. `TransformPushConstantToUBO` is file-static, so test through the public `ReflectSlangSource` with a fixture whose push/UBO block contains a comment with a brace.

- [ ] **Step 1: Write the failing test** — append `TestSlangPushConstantBraceInComment()` to `TestSlangParser.cpp`; register `TEST_ITEM(SlangPushConstantBraceInComment)`. Use a minimal valid two-stage source whose `push_constant` block has a comment containing an unbalanced-looking brace:

```cpp
bool TestSlangPushConstantBraceInComment() {
	SlangSource src;
	src.name = "brace_comment";
	src.vertex =
		"#version 450\n"
		"layout(push_constant) uniform Push {\n"
		"  vec4 SourceSize; // note: use { as a delimiter\n"
		"} params;\n"
		"layout(std140, set=0, binding=0) uniform UBO { mat4 MVP; } global;\n"
		"void main() { gl_Position = global.MVP * vec4(0.0); }\n";
	src.fragment =
		"#version 450\n"
		"layout(push_constant) uniform Push {\n"
		"  vec4 SourceSize; // trailing brace } in comment\n"
		"} params;\n"
		"layout(std140, set=0, binding=0) uniform UBO { mat4 MVP; } global;\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main() { FragColor = vec4(params.SourceSize.x); }\n";
	SlangClassifyContext ctx;
	PassReflection refl; std::string err;
	// Must reflect successfully: the brace in the comment must not derail block extraction.
	EXPECT_TRUE(ReflectSlangSource(src, ctx, &refl, &err));
	// SourceSize must be present as a classified member.
	bool foundSourceSize = false;
	for (const auto &m : refl.uboMembers) if (m.name == "SourceSize") foundSourceSize = true;
	EXPECT_TRUE(foundSourceSize);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — `./build-unittest/PPSSPPUnitTest SlangPushConstantBraceInComment` → FAIL (the `{`/`}` in comments miscount depth; extraction returns the wrong range or `npos`, so reflection fails or omits `SourceSize`).

- [ ] **Step 3: Implement comment/string awareness** — replace `FindMatchingBrace` (`SlangPassCompiler.cpp:70-82`) with:

```cpp
// Helper: find matching closing brace for an opening brace at position 'start'.
// Skips // line comments, /* */ block comments, and "..." string literals so braces
// inside them do not affect depth counting (untrusted shader source may contain them).
static size_t FindMatchingBrace(const std::string &src, size_t start) {
	if (start >= src.size() || src[start] != '{') return std::string::npos;
	int depth = 0;
	bool inLineComment = false, inBlockComment = false, inString = false;
	for (size_t i = start; i < src.size(); i++) {
		char c = src[i];
		if (inString) {
			if (c == '\\' && i + 1 < src.size()) { i++; continue; }  // skip escaped char
			if (c == '"') inString = false;
		} else if (inLineComment) {
			if (c == '\n') inLineComment = false;
		} else if (inBlockComment) {
			if (c == '*' && i + 1 < src.size() && src[i + 1] == '/') { inBlockComment = false; i++; }
		} else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
			inLineComment = true; i++;
		} else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
			inBlockComment = true; i++;
		} else if (c == '"') {
			inString = true;
		} else if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
			if (depth == 0) return i;
		}
	}
	return std::string::npos;
}
```

- [ ] **Step 4: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangPushConstantBraceInComment` → exit 0. Run `SlangPushConstant` (Phase-2 push_constant regression test) → exit 0.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangPassCompiler.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: FindMatchingBrace skips comments and string literals

Brace matching for the push_constant->UBO transform counted { and } that
appear inside // comments, /* */ comments, or string literals, so a shader
with a braced comment inside a uniform block mis-extracted the block range
(reflection failure or wrong members). Track comment/string state while
scanning.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Add `sSlangBuildbotUrl` config key and a `slang/` path helper

**Files:**
- Modify: `Core/Config.h` (near line 379-380, beside `sSlangShaderPreset`)
- Modify: `Core/Config.cpp` (near line 734, beside the `SlangShaderPreset` registration)
- Create: `Core/Slang/SlangPaths.h`
- Create: `Core/Slang/SlangPaths.cpp`
- Modify: the build file lists so the new `.cpp` compiles (see Step 5)

**Interfaces:**
- Produces (Config): `std::string sSlangShaderDir;`-style member `std::string sSlangBuildbotUrl;` on `g_Config`, defaulting to the libretro nightly zip URL.
- Produces (`Core/Slang/SlangPaths.h`):
  ```cpp
  #pragma once
  #include "Common/File/Path.h"
  // Root directory the importer extracts into and the library scans:
  //   <memstick>/PSP/shaders/slang
  Path GetSlangShaderDir();
  ```

- [ ] **Step 1: Add the config member** in `Core/Config.h`, immediately after the existing `std::string sSlangShaderPreset;`:

```cpp
	// URL of the libretro slang-shaders package zip (importer default; overridable for mirrors).
	std::string sSlangBuildbotUrl;
```

- [ ] **Step 2: Register the setting** in `Core/Config.cpp`, immediately after the existing `ConfigSetting("SlangShaderPreset", ...)` line:

```cpp
	ConfigSetting("SlangBuildbotUrl", SETTING(g_Config, sSlangBuildbotUrl),
		"https://buildbot.libretro.com/assets/frontend/shaders_slang.zip", CfgFlag::DEFAULT),
```

- [ ] **Step 3: Create `Core/Slang/SlangPaths.h`** with the GPL 2.0 header (copy from `GPU/Common/Slang/SlangPreset.h`) followed by the interface block above.

- [ ] **Step 4: Create `Core/Slang/SlangPaths.cpp`** (GPL header, then):

```cpp
#include "Core/Slang/SlangPaths.h"
#include "Core/Util/PathUtil.h"

Path GetSlangShaderDir() {
	return GetSysDirectory(DIRECTORY_CUSTOM_SHADERS) / "slang";
}
```

- [ ] **Step 5: Register the new source file in the build.** Add `Core/Slang/SlangPaths.cpp` and `Core/Slang/SlangPaths.h` to the `Core` target in `CMakeLists.txt` (search for an existing `Core/Util/PathUtil.cpp` entry and add the new files near it, following the surrounding formatting). If the repo also lists sources in `libretro/Makefile.common` or `Windows/*.vcxproj`, add them there too (grep for `PathUtil.cpp` to find every list that must be updated).

- [ ] **Step 6: Build to verify** — `cmake --build build-unittest --target PPSSPPUnitTest` → success (the new file compiles and links; `GetSlangShaderDir` is referenced by later tasks). No test yet (pure path composition; covered indirectly by Task 6).

- [ ] **Step 7: Commit**

```bash
git add Core/Config.h Core/Config.cpp Core/Slang/SlangPaths.h Core/Slang/SlangPaths.cpp CMakeLists.txt
git commit -m "slang: add SlangBuildbotUrl config + GetSlangShaderDir() helper

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Zip-slip-safe path sanitizer (pure function + unit tests)

**Files:**
- Modify: `Core/Slang/SlangPaths.h`, `Core/Slang/SlangPaths.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Rationale:** PPSSPP's existing zip extraction (`GameManager`) has **no** path-traversal protection. Since Phase 3 extracts a remote zip, the importer must reject any entry whose resolved path escapes the destination root. This task isolates that logic as a pure, unit-testable function.

**Interfaces:**
- Produces (append to `Core/Slang/SlangPaths.h`):
  ```cpp
  // Given a destination root and a zip entry's internal name, compute the safe on-disk
  // output path. Returns false (reject the entry) if the name is absolute, contains a
  // ".." traversal component, is empty, or the resolved path escapes 'destRoot'.
  // Also returns false for entry names whose extension is not an allowed shader asset
  // (.slang, .slangp, .inc, .h, .png) unless 'isDirectory' is true.
  bool ResolveSafeZipEntryPath(const Path &destRoot, const std::string &entryName,
                               bool isDirectory, Path *outPath);
  ```

- [ ] **Step 1: Write the failing tests** — append `TestSlangZipPathSanitizer()`; register `TEST_ITEM(SlangZipPathSanitizer)`:

```cpp
bool TestSlangZipPathSanitizer() {
	Path root("/tmp/slangroot");
	Path out;
	// Normal file under a category dir is accepted, resolved under root:
	EXPECT_TRUE(ResolveSafeZipEntryPath(root, "crt/crt-royale.slangp", false, &out));
	EXPECT_TRUE(out.StartsWith(root));
	EXPECT_TRUE(out.ToString() == "/tmp/slangroot/crt/crt-royale.slangp");
	// Nested include header accepted:
	EXPECT_TRUE(ResolveSafeZipEntryPath(root, "crt/shaders/x.inc", false, &out));
	// Directory entry accepted regardless of extension:
	EXPECT_TRUE(ResolveSafeZipEntryPath(root, "crt/shaders/", true, &out));
	// Traversal rejected:
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "../evil.slang", false, &out));
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "crt/../../evil.slang", false, &out));
	// Absolute path rejected:
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "/etc/passwd", false, &out));
	// Backslash traversal rejected (Windows-style separators in the zip):
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "..\\evil.slang", false, &out));
	// Disallowed extension rejected (e.g. an executable smuggled in the archive):
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "crt/evil.sh", false, &out));
	// Empty name rejected:
	EXPECT_FALSE(ResolveSafeZipEntryPath(root, "", false, &out));
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — `./build-unittest/PPSSPPUnitTest SlangZipPathSanitizer` → compile error (function undefined).

- [ ] **Step 3: Implement `ResolveSafeZipEntryPath`** in `SlangPaths.cpp`. Add `#include <algorithm>` and `#include "Common/StringUtils.h"` as needed:

```cpp
static bool HasAllowedShaderExtension(const std::string &name) {
	// Case-insensitive check against the shader-asset whitelist.
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	static const char *kExts[] = { ".slang", ".slangp", ".inc", ".h", ".png" };
	for (const char *ext : kExts) {
		size_t elen = strlen(ext);
		if (lower.size() >= elen && lower.compare(lower.size() - elen, elen, ext) == 0)
			return true;
	}
	return false;
}

bool ResolveSafeZipEntryPath(const Path &destRoot, const std::string &entryName,
                             bool isDirectory, Path *outPath) {
	if (entryName.empty()) return false;
	// Reject absolute and drive-relative paths.
	if (entryName[0] == '/' || entryName[0] == '\\') return false;
	if (entryName.size() >= 2 && entryName[1] == ':') return false;  // C:\...
	// Normalize separators and split into components; reject any "..".
	std::string norm = entryName;
	std::replace(norm.begin(), norm.end(), '\\', '/');
	size_t start = 0;
	while (start < norm.size()) {
		size_t slash = norm.find('/', start);
		std::string comp = norm.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (comp == "..") return false;
		start = (slash == std::string::npos) ? norm.size() : slash + 1;
	}
	// Files must have an allowed shader-asset extension; directories may not.
	if (!isDirectory && !HasAllowedShaderExtension(norm)) return false;
	Path resolved = destRoot / norm;
	// Defense in depth: the resolved path must still live under destRoot.
	if (!resolved.StartsWith(destRoot)) return false;
	*outPath = resolved;
	return true;
}
```

- [ ] **Step 4: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangZipPathSanitizer` → exit 0.

- [ ] **Step 5: Commit**

```bash
git add Core/Slang/SlangPaths.h Core/Slang/SlangPaths.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: add zip-slip-safe entry path resolver + tests

PPSSPP's existing zip extraction has no path-traversal defense; the slang
importer will extract a remote zip, so add a pure resolver that rejects
absolute paths, .. traversal, backslash separators, and non-shader
extensions, and verifies the result stays under the destination root.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: `SlangPackageImporter` — extract-to-temp, manifest, atomic swap

**Files:**
- Create: `Core/Slang/SlangPackageImporter.h`
- Create: `Core/Slang/SlangPackageImporter.cpp`
- Modify: `CMakeLists.txt` (+ other source lists per Task 4 Step 5)
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `ResolveSafeZipEntryPath`, `GetSlangShaderDir` (Task 4/5); `ZipOpenPath`/`ZipClose` (`Core/Loaders.h`); libzip `zip_get_num_entries`/`zip_get_name`/`zip_stat_index`/`zip_fopen_index`/`zip_fread`; `File::` ops; `json::JsonWriter`.
- Produces (`SlangPackageImporter.h`):
  ```cpp
  #pragma once
  #include <string>
  #include "Common/File/Path.h"
  // Extract a slang-shaders zip at 'zipPath' into 'destRoot' (default GetSlangShaderDir()),
  // using a temp dir + atomic swap. Only shader-asset files that pass ResolveSafeZipEntryPath
  // are written. On success, writes destRoot/manifest.json (sourceUrl, timestamp, fileCount).
  // Returns false and leaves any prior install untouched on any failure; *error is set.
  // 'sourceUrl' and 'unixTimestamp' are recorded in the manifest (pass "" / 0 if unknown).
  // Synchronous; Task 7 calls this from a worker thread.
  bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                           const std::string &sourceUrl, int64_t unixTimestamp,
                           std::string *error);
  ```

- [ ] **Step 1: Write the failing test** — append `TestSlangPackageExtract()`; register `TEST_ITEM(SlangPackageExtract)`. Build a tiny zip on the fly with libzip (`zip_open(..., ZIP_CREATE)`, `zip_source_buffer`, `zip_file_add`) into a temp path, including one valid entry (`crt/x.slangp`) and one traversal entry (`../evil.slang`), then extract and assert:

```cpp
bool TestSlangPackageExtract() {
	Path tmp = Path(g_Config.memStickDirectory).empty() ? Path("/tmp") : Path("/tmp");
	Path zipPath = tmp / "slang_test_pkg.zip";
	Path dest = tmp / "slang_extract_dest";
	File::DeleteDirRecursively(dest);
	File::Delete(zipPath);

	// --- create the test zip ---
	int zerr = 0;
	zip_t *z = zip_open(zipPath.ToString().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zerr);
	EXPECT_TRUE(z != nullptr);
	auto addFile = [&](const char *name, const std::string &content) {
		zip_source_t *s = zip_source_buffer(z, content.data(), content.size(), 0);
		zip_file_add(z, name, s, ZIP_FL_ENC_UTF_8);
	};
	addFile("crt/x.slangp", "shaders = 0\n");
	addFile("crt/x.slang", "#version 450\n");
	addFile("../evil.slang", "#version 450\n");   // must be rejected
	addFile("crt/notes.txt", "hello");            // wrong ext, must be skipped
	zip_close(z);

	// --- extract ---
	std::string err;
	EXPECT_TRUE(ExtractSlangPackage(zipPath, dest, "http://example/test.zip", 12345, &err));
	// Valid shader files present:
	EXPECT_TRUE(File::Exists(dest / "crt" / "x.slangp"));
	EXPECT_TRUE(File::Exists(dest / "crt" / "x.slang"));
	// Traversal + wrong-ext rejected:
	EXPECT_FALSE(File::Exists(tmp / "evil.slang"));
	EXPECT_FALSE(File::Exists(dest / "crt" / "notes.txt"));
	// Manifest written:
	EXPECT_TRUE(File::Exists(dest / "manifest.json"));
	json::JsonReader r((dest / "manifest.json").ToString());
	EXPECT_TRUE(r.ok());
	std::string url; r.root().getString("sourceUrl", &url);
	EXPECT_TRUE(url == "http://example/test.zip");
	EXPECT_EQ_INT(r.root().getInt("fileCount", -1), 2);

	File::DeleteDirRecursively(dest);
	File::Delete(zipPath);
	return true;
}
```
Add includes to the test file as needed: `<zip.h>`, `Core/Slang/SlangPackageImporter.h`, `Common/Data/Format/JSONReader.h`, `Common/File/FileUtil.h`, `Core/Config.h`.

- [ ] **Step 2: Run to verify it fails** — `./build-unittest/PPSSPPUnitTest SlangPackageExtract` → compile error (`ExtractSlangPackage` undefined).

- [ ] **Step 3: Implement `ExtractSlangPackage`** in `SlangPackageImporter.cpp` (GPL header first). Mirror `GameManager::ExtractFile`'s libzip read loop; extract into `destRoot.GetDirectory() / (destRoot.GetFilename()+".tmp")`, then swap:

```cpp
#include <zip.h>
#include "Core/Slang/SlangPackageImporter.h"
#include "Core/Slang/SlangPaths.h"
#include "Core/Loaders.h"                 // ZipOpenPath / ZipClose
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/JSONWriter.h"
#include "Common/Log.h"

static bool WriteZipEntry(zip_t *z, zip_uint64_t index, const Path &out, std::string *error) {
	struct zip_stat st; zip_stat_init(&st);
	if (zip_stat_index(z, index, 0, &st) != 0) { *error = "zip_stat_index failed"; return false; }
	zip_file_t *zf = zip_fopen_index(z, index, 0);
	if (!zf) { *error = "zip_fopen_index failed"; return false; }
	if (!File::CreateFullPath(Path(out.GetDirectory()))) { zip_fclose(zf); *error = "mkdir failed"; return false; }
	FILE *f = File::OpenCFile(out, "wb");
	if (!f) { zip_fclose(zf); *error = "open output failed"; return false; }
	char buf[128 * 1024];
	zip_uint64_t remaining = st.size;
	bool ok = true;
	while (remaining > 0) {
		zip_int64_t n = zip_fread(zf, buf, sizeof(buf));
		if (n <= 0) { ok = false; break; }
		if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ok = false; break; }
		remaining -= (zip_uint64_t)n;
	}
	fclose(f);
	zip_fclose(zf);
	if (!ok) *error = "zip read/write failed";
	return ok;
}

bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                         const std::string &sourceUrl, int64_t unixTimestamp,
                         std::string *error) {
	Path tempDir = Path(destRoot.GetDirectory()) / (destRoot.GetFilename() + ".import.tmp");
	File::DeleteDirRecursively(tempDir);          // clear any stale temp
	if (!File::CreateFullPath(tempDir)) { *error = "could not create temp dir"; return false; }

	zip_t *z = ZipOpenPath(zipPath);
	if (!z) { *error = "could not open downloaded zip"; File::DeleteDirRecursively(tempDir); return false; }

	int fileCount = 0;
	zip_int64_t numEntries = zip_get_num_entries(z, 0);
	for (zip_int64_t i = 0; i < numEntries; i++) {
		const char *name = zip_get_name(z, (zip_uint64_t)i, 0);
		if (!name) continue;
		std::string entry(name);
		bool isDir = !entry.empty() && (entry.back() == '/' || entry.back() == '\\');
		Path outPath;
		if (!ResolveSafeZipEntryPath(tempDir, entry, isDir, &outPath)) {
			continue;  // silently skip rejected entries (traversal, wrong ext, metadata)
		}
		if (isDir) { File::CreateFullPath(outPath); continue; }
		if (!WriteZipEntry(z, (zip_uint64_t)i, outPath, error)) {
			ZipClose(z); File::DeleteDirRecursively(tempDir); return false;
		}
		fileCount++;
	}
	ZipClose(z);

	if (fileCount == 0) { *error = "no valid shader files in archive"; File::DeleteDirRecursively(tempDir); return false; }

	// Write manifest inside the temp dir so it swaps atomically with the content.
	{
		json::JsonWriter w(json::JsonWriter::PRETTY);
		w.begin();
		w.writeString("sourceUrl", sourceUrl);
		w.writeInt("timestamp", (int)unixTimestamp);
		w.writeInt("fileCount", fileCount);
		w.end();
		File::WriteStringToFile(true, w.str(), tempDir / "manifest.json");
	}

	// Atomic-ish swap: remove any existing install, then move temp into place.
	File::DeleteDirRecursively(destRoot);
	if (!File::Move(tempDir, destRoot)) {
		*error = "could not move imported shaders into place";
		File::DeleteDirRecursively(tempDir);
		return false;
	}
	return true;
}
```
Notes for the implementer: `ResolveSafeZipEntryPath` is declared in `SlangPaths.h`; include it. `w.writeInt` takes an `int` — the `(int)unixTimestamp` cast in the manifest matches the reader's `getInt`; that is sufficient for this phase (a 32-bit second-resolution timestamp). If `JsonWriter` lacks `writeInt`, use `writeFloat`/`writeString` and adjust the test accordingly (verify the actual API in `Common/Data/Format/JSONWriter.h` before implementing).

- [ ] **Step 4: Register the new source file** in `CMakeLists.txt` (and any other lists per Task 4 Step 5).

- [ ] **Step 5: Run to verify it passes** — `cmake --build build-unittest --target PPSSPPUnitTest` then `./build-unittest/PPSSPPUnitTest SlangPackageExtract` → exit 0.

- [ ] **Step 6: Commit**

```bash
git add Core/Slang/SlangPackageImporter.h Core/Slang/SlangPackageImporter.cpp CMakeLists.txt unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: package extractor with zip-slip guard, manifest, atomic swap

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: `SlangPackageImporter` — download lifecycle + poll API

**Files:**
- Modify: `Core/Slang/SlangPackageImporter.h`, `Core/Slang/SlangPackageImporter.cpp`

**Rationale:** Wrap the synchronous `ExtractSlangPackage` in a `GameManager`-style poll-driven lifecycle: start a download, drive it from `Update()`, kick extraction on a worker thread when the download finishes, and expose progress/state/error. No unit test (needs live network + `g_DownloadManager` pump); verified by the on-device UI task (Task 8). Keep the state machine tiny and mirror `GameManager` exactly.

**Interfaces:**
- Produces (append to `SlangPackageImporter.h`):
  ```cpp
  #include <memory>
  #include <thread>
  #include <atomic>
  namespace http { class Request; }

  enum class SlangImportState { IDLE, DOWNLOADING, EXTRACTING, DONE, FAILED };

  class SlangPackageImporter {
  public:
      ~SlangPackageImporter();
      // Begins download+import of 'url' (default g_Config.sSlangBuildbotUrl if empty).
      // Returns false if an import is already in progress.
      bool Start(const std::string &url);
      // Pump each frame from the UI thread (drives download completion + thread join).
      void Update();
      SlangImportState GetState() const { return state_; }
      float GetProgress() const;                 // 0..1 across download (extraction is coarse)
      std::string GetError() const { return error_; }
      bool Busy() const { return state_ == SlangImportState::DOWNLOADING || state_ == SlangImportState::EXTRACTING; }
  private:
      SlangImportState state_ = SlangImportState::IDLE;
      std::shared_ptr<http::Request> download_;
      std::thread extractThread_;
      std::atomic<bool> extractDone_{false};
      std::atomic<bool> extractOk_{false};
      std::string error_;
      std::string sourceUrl_;
      Path zipPath_;
  };

  // Process-wide instance (mirrors g_GameManager).
  extern SlangPackageImporter g_SlangImporter;
  ```

- [ ] **Step 1: Implement the lifecycle** in `SlangPackageImporter.cpp`. Add includes: `Core/Config.h`, `Common/Net/HTTPRequest.h`, `Core/Util/GameManager.h` (only if reusing `GetTempFilename`; otherwise compose a temp path from `g_Config.memStickDirectory / "ppsspp_slang.dl"`). Define `SlangPackageImporter g_SlangImporter;`.

```cpp
bool SlangPackageImporter::Start(const std::string &url) {
	if (Busy()) return false;
	sourceUrl_ = url.empty() ? g_Config.sSlangBuildbotUrl : url;
	if (sourceUrl_.empty()) { error_ = "no buildbot URL configured"; state_ = SlangImportState::FAILED; return false; }
	zipPath_ = Path(g_Config.memStickDirectory) / "ppsspp_slang.dl";
	error_.clear();
	extractDone_ = false;
	extractOk_ = false;
	download_ = g_DownloadManager.StartDownload(sourceUrl_, zipPath_,
		http::RequestFlags::ProgressBar | http::RequestFlags::ProgressBarDelayed,
		"application/zip", "slang_shaders");
	if (!download_) { error_ = "failed to start download"; state_ = SlangImportState::FAILED; return false; }
	state_ = SlangImportState::DOWNLOADING;
	return true;
}

void SlangPackageImporter::Update() {
	if (state_ == SlangImportState::DOWNLOADING) {
		if (download_ && download_->Done()) {
			bool ok = !download_->Failed() && download_->ResultCode() == 200 && File::Exists(zipPath_);
			std::shared_ptr<http::Request> finished = download_;
			download_.reset();
			if (!ok) {
				error_ = "download failed (HTTP " + std::to_string(finished ? finished->ResultCode() : 0) + ")";
				File::Delete(zipPath_);
				state_ = SlangImportState::FAILED;
				return;
			}
			// Kick extraction on a worker thread (extraction can be slow).
			state_ = SlangImportState::EXTRACTING;
			std::string src = sourceUrl_;
			Path zip = zipPath_;
			extractThread_ = std::thread([this, zip, src]() {
				std::string err;
				// NOTE: pass 0 for timestamp; Date/time is not available in this layer without
				// plumbing. A wall-clock stamp can be added later; fileCount+url suffice for now.
				bool ok = ExtractSlangPackage(zip, GetSlangShaderDir(), src, 0, &err);
				if (!ok) error_ = err;   // written before extractDone_ is set (happens-before via release)
				extractOk_ = ok;
				extractDone_ = true;
			});
		}
	} else if (state_ == SlangImportState::EXTRACTING) {
		if (extractDone_.load()) {
			if (extractThread_.joinable()) extractThread_.join();
			File::Delete(zipPath_);
			state_ = extractOk_.load() ? SlangImportState::DONE : SlangImportState::FAILED;
		}
	}
}

float SlangPackageImporter::GetProgress() const {
	if (state_ == SlangImportState::DOWNLOADING && download_) return download_->Progress() * 0.9f;
	if (state_ == SlangImportState::EXTRACTING) return 0.95f;
	if (state_ == SlangImportState::DONE) return 1.0f;
	return 0.0f;
}

SlangPackageImporter::~SlangPackageImporter() {
	if (extractThread_.joinable()) extractThread_.join();
}
```
Implementer note: confirm `http::Request` exposes `Done()/Failed()/ResultCode()/Progress()` (it does — see the verbatim reference block). The `error_` write in the worker then read on the UI thread after `extractDone_` is safe because `extractDone_` is an atomic acting as a release/acquire fence.

- [ ] **Step 2: Build to verify** — `cmake --build build-unittest --target PPSSPPUnitTest` → success (links against `g_DownloadManager`; the unit-test binary already links `Core`). Re-run all prior slang tests → all exit 0 (no regression).

- [ ] **Step 3: Commit**

```bash
git add Core/Slang/SlangPackageImporter.h Core/Slang/SlangPackageImporter.cpp
git commit -m "slang: poll-driven download+extract lifecycle (g_SlangImporter)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Minimal UI trigger + `Update()` pump

**Files:**
- Modify: `UI/GameSettingsScreen.cpp` (Graphics section, near the existing post-shader chooser row)
- Modify: whichever screen's `update()` already pumps `g_DownloadManager.Update()` per frame, OR the same screen that owns the new button — to also call `g_SlangImporter.Update()` (find the existing `g_DownloadManager.Update()` call site with grep; the importer pump must run on the same UI thread every frame while a screen that can trigger it is active).

**Rationale:** Phase 3's milestone is "one-click download from buildbot"; a full two-level browser is Phase 4. So this task adds only a single button + status text, not the `SlangShaderScreen`. Keep it minimal and consistent with existing settings rows.

**Interfaces:**
- Consumes: `g_SlangImporter` (Task 7), `g_Config.sSlangBuildbotUrl`.

- [ ] **Step 1: Find the pump site** — `grep -rn "g_DownloadManager.Update" UI/ Core/` to locate where downloads are already pumped each frame. Confirm the screen that will host the button stays active during the download; add `g_SlangImporter.Update();` adjacent to the existing pump (or in that screen's `update()`).

- [ ] **Step 2: Add the settings row** in the Graphics section of `GameSettingsScreen.cpp`, near the post-shader chooser. Add a `Choice` button labeled e.g. "Import RetroArch (slang) shaders" whose handler calls `g_SlangImporter.Start("")` (empty → uses configured URL). Follow the surrounding `graphicsSettings->Add(new Choice(...))->OnClick.Handle(...)` pattern already used in that file (copy an adjacent Choice row's exact construction and localization-string usage). In the handler, if `g_SlangImporter.Busy()`, no-op; else `Start`.

- [ ] **Step 3: Add status feedback** — the simplest correct approach that matches existing code: in the button's `OnClick`, after `Start`, show an OSD notice (`g_OSD.Show(...)` or the notice API used elsewhere in this file) reading "Downloading slang shaders…". Optionally, drive `g_OSD.SetProgressBar("slang_import", ..., g_SlangImporter.GetProgress(), ...)` from the pump site (Step 1) while `Busy()`, and show a completion/failure notice when the state transitions to `DONE`/`FAILED` (track the previous state in the screen to fire the notice once). Mirror the exact OSD API used by `GameManager`/`InstallZipScreen` — verify the signatures in those files before writing.

- [ ] **Step 4: Build + on-device verification** — build+install the APK. In Graphics settings, tap the new row; confirm: (a) a progress indication appears, (b) after completion, `<memstick>/PSP/shaders/slang/` exists with a category tree and `manifest.json` (verify via `adb shell ls`), (c) selecting a preset from the imported tree (by setting `sSlangShaderPreset` to an imported `.slangp` path) renders through the existing chain. Record these confirmations in the commit body.

- [ ] **Step 5: Commit**

```bash
git add UI/GameSettingsScreen.cpp <pump-site-file>
git commit -m "slang: add import trigger in Graphics settings + importer pump

One-click download of the libretro slang-shaders package into
PSP/shaders/slang. Full category-browsing UI is Phase 4. Verified on-device:
download completes, tree + manifest.json land, imported preset renders.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Out of scope for Phase 3 (deferred to later phases per the design doc §9)

- **SlangPresetLibrary** (category/preset index, search) and the full two-level **SlangShaderScreen** browser with per-preset parameter sliders — **Phase 4**.
- **Backend expansion** (D3D11, OpenGL/GLES) — **Phase 5**.
- Resumable/mirror-list downloads, update-available detection beyond the single `manifest.json`, and disk-cached compiled SPIR-V — later optimization passes.

## Self-Review (completed during authoring)

- **Design §8 coverage:** download (Task 7), safe unpack into `slang/` (Tasks 5–6), manifest.json with source URL + timestamp + file count (Task 6), atomicity via temp-dir + swap (Task 6), integrity via HTTP-200 + zip-open checks (Tasks 6–7), progress polling (Task 7), minimal import UI (Task 8). Zip-slip protection — a gap the design assumed but PPSSPP lacks — is explicitly added (Task 5).
- **Security prerequisite:** Tasks 1–3 fold in the three confirmed code-review findings, all of which are untrusted-input hardening that becomes live the moment Phase 3 loads downloaded shaders. They gate the importer work and each ships with a regression-safe test.
- **Type consistency:** `ResolveSafeZipEntryPath`, `ExtractSlangPackage`, `GetSlangShaderDir`, and `g_SlangImporter` signatures are used consistently across Tasks 4→8. Manifest keys (`sourceUrl`, `timestamp`, `fileCount`) match between writer (Task 6) and the test reader.
- **Placeholder scan:** every code step carries concrete code; the two intentionally deferred implementer decisions (exact OSD notice API in Task 8; `JsonWriter::writeInt` availability in Task 6) are called out as explicit "verify in-file before writing" instructions rather than vague TODOs, because they depend on APIs the implementer must read at that moment.

