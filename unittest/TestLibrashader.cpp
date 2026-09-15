// Copyright (c) 2026- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cstdlib>
#include <cstring>
#include <string>
#include "ppsspp_config.h"
#include "unittest/UnitTest.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#include "Core/ConfigValues.h"

// Device-free: with LIBRASHADER_PATH pointing at a non-library, Load must fail cleanly,
// report an error string and leave IsLoaded() false. Load must be idempotent.
bool TestLibrashaderLoaderAbsent() {
#if USE_LIBRASHADER && !PPSSPP_PLATFORM(WINDOWS)
	setenv("LIBRASHADER_PATH", "/nonexistent/dir/librashader.dylib", 1);
	Librashader::Unload();
	std::string err;
	EXPECT_FALSE(Librashader::Load(&err));
	EXPECT_FALSE(Librashader::IsLoaded());
	EXPECT_FALSE(err.empty());
	std::string err2;
	EXPECT_FALSE(Librashader::Load(&err2));  // second call: same answer, no crash
	EXPECT_FALSE(Librashader::IsLoaded());
	unsetenv("LIBRASHADER_PATH");
	Librashader::Unload();
#endif
	return true;
}

// Device-free: LIBRASHADER_PATH pointing at a file that exists but is not a loadable library must
// also fail cleanly (dlopen fails rather than the file being missing).
bool TestLibrashaderLoaderNotALibrary() {
#if USE_LIBRASHADER && !PPSSPP_PLATFORM(WINDOWS)
	const Path bogus("/tmp/ppsspp-test-not-a-librashader.bin");
	EXPECT_TRUE(File::WriteStringToFile(false, "this is not a shared library\n", bogus));
	setenv("LIBRASHADER_PATH", bogus.c_str(), 1);
	Librashader::Unload();
	std::string err;
	EXPECT_FALSE(Librashader::Load(&err));
	EXPECT_FALSE(Librashader::IsLoaded());
	EXPECT_FALSE(err.empty());
	unsetenv("LIBRASHADER_PATH");
	Librashader::Unload();
	File::Delete(bogus);
#endif
	return true;
}

bool TestSlangChainBackendSelection() {
	// Librashader is the only rendering core: selected when every precondition holds.
	EXPECT_TRUE(ChooseSlangChainBackend(true, GPUBackend::VULKAN, true) == SlangChainBackend::Librashader);
	EXPECT_TRUE(ChooseSlangChainBackend(true, GPUBackend::OPENGL, true) == SlangChainBackend::Librashader);
	EXPECT_TRUE(ChooseSlangChainBackend(true, GPUBackend::DIRECT3D11, true) == SlangChainBackend::Librashader);
	// Any missing precondition means no chain at all (the raw image is presented).
	EXPECT_TRUE(ChooseSlangChainBackend(false, GPUBackend::VULKAN, true) == SlangChainBackend::None);
	EXPECT_TRUE(ChooseSlangChainBackend(true, GPUBackend::VULKAN, false) == SlangChainBackend::None);
	EXPECT_TRUE(ChooseSlangChainBackend(true, GPUBackend::DIRECT3D11, false) == SlangChainBackend::None);
	EXPECT_TRUE(ChooseSlangChainBackend(true, (GPUBackend)1, true) == SlangChainBackend::None);  // retired D3D9 slot: not a supported backend
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::None), "none") == 0);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::Librashader), "librashader") == 0);
	return true;
}

// Device-free: the pure token scans LibrashaderFilterChain::Load() uses to pick the input mode.
// Both must match whole identifiers only, so a longer name that merely contains the needle does not
// change the input mode (and with it the rendered result) of an unrelated preset.
bool TestLibrashaderSourceScan() {
#if USE_LIBRASHADER
	// SourceSize / OriginalSize: the reads that make a preset's output depend on the input size.
	EXPECT_TRUE(ReferencesSourceSize("vec2 s = params.SourceSize.xy;"));
	EXPECT_TRUE(ReferencesSourceSize("uv * OriginalSize.zw"));
	EXPECT_TRUE(ReferencesSourceSize("vec4 s = global.SourceSize;"));   // read, then ';'
	EXPECT_TRUE(ReferencesSourceSize("vec4 s = SourceSize;"));          // anonymous block read
	EXPECT_TRUE(ReferencesSourceSize("vec4 SourceSize;\nvec2 p = SourceSize.xy;"));  // declared and read
	// The Push/UBO block declaration alone is not a read - nearly every shader has one.
	EXPECT_FALSE(ReferencesSourceSize("\tvec4 SourceSize;\n"));
	EXPECT_FALSE(ReferencesSourceSize("layout(push_constant) uniform Push {\n\tvec4 SourceSize;\n\tvec4 OriginalSize ;\n\tvec4 OutputSize;\n} params;"));
	EXPECT_FALSE(ReferencesSourceSize("FinalViewportSize"));
	EXPECT_FALSE(ReferencesSourceSize("OriginalHistorySize1"));
	EXPECT_FALSE(ReferencesSourceSize("mySourceSizeHack"));   // glued in front
	EXPECT_FALSE(ReferencesSourceSize("SourceSizes[2]"));     // glued behind
	EXPECT_FALSE(ReferencesSourceSize("texture(Source, uv)"));
	EXPECT_FALSE(ReferencesSourceSize(""));

	// OriginalHistoryN / OriginalHistorySizeN: index 0 is the current frame, so it does not count.
	EXPECT_TRUE(ReferencesOriginalHistory("texture(OriginalHistory1, uv)"));
	EXPECT_TRUE(ReferencesOriginalHistory("params.OriginalHistorySize2.xy"));
	EXPECT_TRUE(ReferencesOriginalHistory("OriginalHistory9"));
	EXPECT_FALSE(ReferencesOriginalHistory("texture(OriginalHistory0, uv)"));
	EXPECT_FALSE(ReferencesOriginalHistory("texture(Original, uv)"));
	EXPECT_FALSE(ReferencesOriginalHistory("MyOriginalHistory1"));  // glued in front
	EXPECT_FALSE(ReferencesOriginalHistory("OriginalHistory"));
	EXPECT_FALSE(ReferencesOriginalHistory(""));
#endif
	return true;
}
