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

#include <string>
#include "unittest/UnitTest.h"
#include "Common/File/Path.h"
#include "GPU/Common/Slang/SlangpParser.h"

bool TestSlangParser() {
	// Two-pass preset with per-axis scale, alias, and a parameter list line.
	const std::string preset =
		"shaders = 2\n"
		"shader0 = shaders/first.slang\n"
		"filter_linear0 = true\n"
		"scale_type0 = source\n"
		"scale0 = 1.0\n"
		"alias0 = FirstPass\n"
		"shader1 = shaders/second.slang\n"
		"scale_type_x1 = viewport\n"
		"scale_x1 = 1.0\n"
		"scale_type_y1 = absolute\n"
		"scale_y1 = 240\n";

	SlangPreset out;
	std::string error;
	Path base("/tmp/presetdir");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &error));
	EXPECT_EQ_INT((int)out.passes.size(), 2);

	// Pass 0
	std::string p0 = out.passes[0].shaderPath;
	std::string expect0 = (base / "shaders/first.slang").ToString();
	EXPECT_EQ_STR(p0, expect0);
	EXPECT_TRUE(out.passes[0].filterLinear);
	EXPECT_TRUE(out.passes[0].scaleTypeX == SlangScaleType::Source);
	EXPECT_TRUE(out.passes[0].scaleTypeY == SlangScaleType::Source);
	EXPECT_EQ_FLOAT(out.passes[0].scaleX, 1.0f);
	std::string a0 = out.passes[0].alias;
	std::string expectA0 = "FirstPass";
	EXPECT_EQ_STR(a0, expectA0);

	// Pass 1: per-axis override
	EXPECT_FALSE(out.passes[1].filterLinear);
	EXPECT_TRUE(out.passes[1].scaleTypeX == SlangScaleType::Viewport);
	EXPECT_TRUE(out.passes[1].scaleTypeY == SlangScaleType::Absolute);
	EXPECT_EQ_FLOAT(out.passes[1].scaleY, 240.0f);

	// Missing shaders count -> error
	SlangPreset bad;
	std::string badErr;
	EXPECT_FALSE(ParseSlangPreset("shader0 = x.slang\n", base, &bad, &badErr));
	return true;
}
