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

bool TestSlangChainBackendSelection() {
	// Librashader only when every precondition holds.
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::VULKAN, true) == SlangChainBackend::Librashader);
	// Any missing precondition falls back to the in-tree chain.
	EXPECT_TRUE(ChooseSlangChainBackend(false, true, GPUBackend::VULKAN, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, false, GPUBackend::VULKAN, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::VULKAN, false) == SlangChainBackend::InTree);
	// Phase 1: Vulkan only.
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::OPENGL, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::DIRECT3D11, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::InTree), "in-tree") == 0);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::Librashader), "librashader") == 0);
	return true;
}
