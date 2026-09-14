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
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::None), "none") == 0);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::Librashader), "librashader") == 0);
	return true;
}
