// Copyright (c) 2021- PPSSPP Project.

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

#pragma once

#include "ppsspp_config.h"

#include <string>
#include <unordered_map>
#if PPSSPP_ARCH(ARM)
#include "Common/ArmEmitter.h"
#elif PPSSPP_ARCH(ARM64)
#include "Common/Arm64Emitter.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Common/x64Emitter.h"
#elif PPSSPP_ARCH(MIPS)
#include "Common/MipsEmitter.h"
#else
#include "Common/FakeEmitter.h"
#endif
#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"

namespace Rasterizer {

typedef void (*SingleFunc)(int x, int y, int z, int fog, const Math3D::Vec4<int> &color_in, const PixelFuncID &pixelID);
SingleFunc GetSingleFunc(const PixelFuncID &id);

void Init();
void Shutdown();

bool DescribeCodePtr(const u8 *ptr, std::string &name);

#if PPSSPP_ARCH(ARM)
class PixelJitCache : public ArmGen::ARMXCodeBlock {
#elif PPSSPP_ARCH(ARM64)
class PixelJitCache : public Arm64Gen::ARM64CodeBlock {
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
class PixelJitCache : public Gen::XCodeBlock {
#elif PPSSPP_ARCH(MIPS)
class PixelJitCache : public MIPSGen::MIPSCodeBlock {
#else
class PixelJitCache : public FakeGen::FakeXCodeBlock {
#endif
public:
	PixelJitCache();

	// Returns a pointer to the code to run.
	SingleFunc GetSingle(const PixelFuncID &id);
	void Clear();

	std::string DescribeCodePtr(const u8 *ptr);

private:
	SingleFunc CompileSingle(const PixelFuncID &id);

#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif

	std::unordered_map<PixelFuncID, SingleFunc> cache_;
	std::unordered_map<PixelFuncID, const u8 *> addresses_;
};

};
