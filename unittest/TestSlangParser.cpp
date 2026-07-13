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

	// Regression: # inside quotes should not be treated as comment start
	const std::string hashPreset =
		"shaders = 1\n"
		"shader0 = \"path#hash.slang\"\n"
		"filter_linear0 = true # this is a comment\n";
	SlangPreset hashOut;
	std::string hashErr;
	EXPECT_TRUE(ParseSlangPreset(hashPreset, base, &hashOut, &hashErr));
	EXPECT_EQ_INT((int)hashOut.passes.size(), 1);
	std::string hashPath = hashOut.passes[0].shaderPath;
	std::string expectHash = (base / "path#hash.slang").ToString();
	EXPECT_EQ_STR(hashPath, expectHash);
	EXPECT_TRUE(hashOut.passes[0].filterLinear);

	return true;
}

bool TestSlangSplit() {
	const std::string src =
		"#version 450\n"
		"layout(set=0,binding=0,std140) uniform UBO { vec4 SourceSize; float ColorMod; };\n"
		"#pragma name StockShader\n"
		"#pragma parameter ColorMod \"Color intensity\" 1.0 0.1 2.0 0.1\n"
		"#pragma stage vertex\n"
		"void main() { gl_Position = vec4(0.0); }\n"
		"#pragma stage fragment\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main() { FragColor = vec4(ColorMod); }\n";

	SlangSource out;
	std::string error;
	EXPECT_TRUE(SplitSlangSource(src, &out, &error));

	std::string name = out.name;
	std::string expectName = "StockShader";
	EXPECT_EQ_STR(name, expectName);

	// Shared prologue (#version + UBO) present in BOTH stages.
	EXPECT_TRUE(out.vertex.find("#version 450") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("#version 450") != std::string::npos);
	EXPECT_TRUE(out.vertex.find("uniform UBO") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("uniform UBO") != std::string::npos);

	// Stage bodies land in the right stage only.
	EXPECT_TRUE(out.vertex.find("gl_Position") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("gl_Position") == std::string::npos);
	EXPECT_TRUE(out.fragment.find("FragColor") != std::string::npos);
	EXPECT_TRUE(out.vertex.find("FragColor") == std::string::npos);

	// #pragma lines are stripped from emitted GLSL.
	EXPECT_TRUE(out.fragment.find("#pragma") == std::string::npos);

	// Parameter parsed.
	EXPECT_EQ_INT((int)out.params.size(), 1);
	std::string pn = out.params[0].name;
	std::string expectPn = "ColorMod";
	EXPECT_EQ_STR(pn, expectPn);
	EXPECT_EQ_FLOAT(out.params[0].initial, 1.0f);
	EXPECT_EQ_FLOAT(out.params[0].minimum, 0.1f);
	EXPECT_EQ_FLOAT(out.params[0].maximum, 2.0f);
	EXPECT_EQ_FLOAT(out.params[0].step, 0.1f);
	return true;
}
