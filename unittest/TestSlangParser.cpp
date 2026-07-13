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
#include "GPU/Common/Slang/SlangResolution.h"
#include "GPU/Common/Slang/SlangReflection.h"
#include "GPU/Common/Slang/SlangPassCompiler.h"
#include "Common/GPU/ShaderTranslation.h"

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

bool TestSlangResolution() {
	SlangSize input{480, 272};
	SlangSize viewport{1920, 1080};

	SlangPassDesc a;  // source x2 both axes
	a.scaleTypeX = a.scaleTypeY = SlangScaleType::Source;
	a.scaleX = a.scaleY = 2.0f;
	SlangSize ra = ResolvePassSize(a, input, viewport);
	EXPECT_EQ_INT(ra.w, 960);
	EXPECT_EQ_INT(ra.h, 544);

	SlangPassDesc b;  // x = viewport 1.0, y = absolute 240
	b.scaleTypeX = SlangScaleType::Viewport; b.scaleX = 1.0f;
	b.scaleTypeY = SlangScaleType::Absolute; b.scaleY = 240.0f;
	SlangSize rb = ResolvePassSize(b, input, viewport);
	EXPECT_EQ_INT(rb.w, 1920);
	EXPECT_EQ_INT(rb.h, 240);

	SlangPassDesc c;  // degenerate scale clamps to 1
	c.scaleTypeX = c.scaleTypeY = SlangScaleType::Source;
	c.scaleX = c.scaleY = 0.0f;
	SlangSize rc = ResolvePassSize(c, input, viewport);
	EXPECT_EQ_INT(rc.w, 1);
	EXPECT_EQ_INT(rc.h, 1);
	return true;
}

bool TestSlangSemantics() {
	std::vector<std::string> params = { "ColorMod", "Sharpness" };

	EXPECT_TRUE(ClassifyUniform("MVP", params) == SlangSemantic::MVP);
	EXPECT_TRUE(ClassifyUniform("SourceSize", params) == SlangSemantic::SourceSize);
	EXPECT_TRUE(ClassifyUniform("OriginalSize", params) == SlangSemantic::OriginalSize);
	EXPECT_TRUE(ClassifyUniform("OutputSize", params) == SlangSemantic::OutputSize);
	EXPECT_TRUE(ClassifyUniform("FinalViewportSize", params) == SlangSemantic::FinalViewportSize);
	EXPECT_TRUE(ClassifyUniform("FrameCount", params) == SlangSemantic::FrameCount);
	EXPECT_TRUE(ClassifyUniform("ColorMod", params) == SlangSemantic::UserParameter);
	EXPECT_TRUE(ClassifyUniform("Sharpness", params) == SlangSemantic::UserParameter);
	EXPECT_TRUE(ClassifyUniform("SomethingElse", params) == SlangSemantic::Unknown);

	EXPECT_TRUE(ClassifyTexture("Source") == SlangSemantic::TexSource);
	EXPECT_TRUE(ClassifyTexture("Original") == SlangSemantic::TexOriginal);
	// Not supported in Phase 1:
	EXPECT_TRUE(ClassifyTexture("PassOutput0") == SlangSemantic::Unknown);
	return true;
}

bool TestSlangReflection() {
	ShaderTranslationInit();  // idempotent-safe within a single test run
	const std::string srcText =
		"#version 450\n"
		"layout(set=0,binding=0,std140) uniform UBO {\n"
		"  mat4 MVP;\n"
		"  vec4 SourceSize;\n"
		"  float ColorMod;\n"
		"};\n"
		"#pragma parameter ColorMod \"Color\" 1.0 0.1 2.0 0.1\n"
		"#pragma stage vertex\n"
		"layout(location=0) in vec4 Position;\n"
		"layout(location=1) in vec2 TexCoord;\n"
		"layout(location=0) out vec2 vTexCoord;\n"
		"void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }\n"
		"#pragma stage fragment\n"
		"layout(location=0) in vec2 vTexCoord;\n"
		"layout(location=0) out vec4 FragColor;\n"
		"layout(binding=1) uniform sampler2D Source;\n"
		"void main() { FragColor = texture(Source, vTexCoord) * ColorMod; }\n";

	SlangSource src;
	std::string error;
	EXPECT_TRUE(SplitSlangSource(srcText, &src, &error));

	PassReflection refl;
	EXPECT_TRUE(ReflectSlangSource(src, &refl, &error));

	// UBO binding 0, three members with correct semantics + offsets (std140).
	EXPECT_EQ_INT(refl.uboBinding, 0);
	EXPECT_EQ_INT((int)refl.uboMembers.size(), 3);

	bool sawMVP = false, sawSourceSize = false, sawColorMod = false;
	for (const auto &m : refl.uboMembers) {
		if (m.semantic == SlangSemantic::MVP) { sawMVP = true; EXPECT_EQ_INT((int)m.offsetBytes, 0); EXPECT_EQ_INT((int)m.sizeBytes, 64); }
		if (m.semantic == SlangSemantic::SourceSize) { sawSourceSize = true; EXPECT_EQ_INT((int)m.offsetBytes, 64); EXPECT_EQ_INT((int)m.sizeBytes, 16); }
		if (m.semantic == SlangSemantic::UserParameter) { sawColorMod = true; EXPECT_EQ_INT((int)m.offsetBytes, 80); }
	}
	EXPECT_TRUE(sawMVP); EXPECT_TRUE(sawSourceSize); EXPECT_TRUE(sawColorMod);

	// Sampler "Source" at binding 1, classified TexSource.
	EXPECT_EQ_INT((int)refl.textures.size(), 1);
	EXPECT_EQ_INT(refl.textures[0].binding, 1);
	EXPECT_TRUE(refl.textures[0].semantic == SlangSemantic::TexSource);
	return true;
}

bool TestSlangParserPhase2Keys() {
	const std::string preset =
		"shaders = 2\n"
		"feedback_pass = 0\n"
		"shader0 = a.slang\n"
		"srgb_framebuffer0 = true\n"
		"mipmap_input0 = true\n"
		"wrap_mode0 = repeat\n"
		"frame_count_mod0 = 60\n"
		"shader1 = b.slang\n"
		"float_framebuffer1 = true\n"
		"wrap_mode1 = clamp_to_edge\n";
	SlangPreset out; std::string err; Path base("/tmp/x");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &err));
	EXPECT_EQ_INT(out.feedbackPass, 0);
	EXPECT_TRUE(out.passes[0].srgbFramebuffer);
	EXPECT_FALSE(out.passes[0].floatFramebuffer);
	EXPECT_TRUE(out.passes[0].mipmapInput);
	EXPECT_TRUE(out.passes[0].wrapMode == SlangWrapMode::Repeat);
	EXPECT_EQ_INT(out.passes[0].frameCountMod, 60);
	EXPECT_TRUE(out.passes[1].floatFramebuffer);
	EXPECT_TRUE(out.passes[1].wrapMode == SlangWrapMode::ClampToEdge);
	// Defaults on an unspecified pass field:
	EXPECT_TRUE(out.passes[1].wrapMode != SlangWrapMode::ClampToBorder);  // it was set
	EXPECT_FALSE(out.passes[1].srgbFramebuffer);
	return true;
}

bool TestSlangParserLuts() {
	const std::string preset =
		"shaders = 1\n"
		"shader0 = a.slang\n"
		"textures = \"LUT1;LUT2\"\n"
		"LUT1 = ../luts/one.png\n"
		"LUT1_linear = true\n"
		"LUT1_mipmap = true\n"
		"LUT1_wrap_mode = repeat\n"
		"LUT2 = two.png\n";
	SlangPreset out; std::string err; Path base("/tmp/dir");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &err));
	EXPECT_EQ_INT((int)out.luts.size(), 2);
	std::string n0 = out.luts[0].name; std::string e0 = "LUT1"; EXPECT_EQ_STR(n0, e0);
	std::string p0 = out.luts[0].path; std::string ep0 = (base / "../luts/one.png").ToString(); EXPECT_EQ_STR(p0, ep0);
	EXPECT_TRUE(out.luts[0].linear);
	EXPECT_TRUE(out.luts[0].mipmap);
	EXPECT_TRUE(out.luts[0].wrapMode == SlangWrapMode::Repeat);
	std::string n1 = out.luts[1].name; std::string e1 = "LUT2"; EXPECT_EQ_STR(n1, e1);
	EXPECT_FALSE(out.luts[1].linear);
	return true;
}
