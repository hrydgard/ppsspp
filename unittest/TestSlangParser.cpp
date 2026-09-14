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
#include <map>
#include <zip.h>
#include <algorithm>
#include "unittest/UnitTest.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/JSONReader.h"
#include "Core/Config.h"
#include "GPU/Common/Slang/SlangpParser.h"
#include "Core/Slang/SlangPaths.h"
#include "Core/Slang/SlangPackageImporter.h"
#include "Core/Slang/SlangPresetLibrary.h"

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

bool TestSlangFormatPragma() {
	const std::string src =
		"#version 450\n"
		"#pragma format R16G16B16A16_SFLOAT\n"
		"#pragma stage vertex\n"
		"void main(){ gl_Position = vec4(0.0); }\n"
		"#pragma stage fragment\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main(){ FragColor = vec4(1.0); }\n";
	SlangSource out; std::string err;
	EXPECT_TRUE(SplitSlangSource(src, &out, &err));
	EXPECT_TRUE(out.format == SlangFbFormat::Float);
	// #pragma format line must NOT leak into emitted GLSL:
	EXPECT_TRUE(out.fragment.find("#pragma format") == std::string::npos);
	return true;
}

bool TestSlangIncludes() {
	// Fake filesystem reader backed by a map
	std::map<std::string, std::string> files;
	files["/base/common.inc"] = "float helper() { return 1.0; }";
	files["/base/nested.inc"] = "#include \"deep.inc\"\nfloat middle() { return 2.0; }";
	files["/base/deep.inc"] = "float deep() { return 3.0; }";
	files["/base/cycle.inc"] = "#include \"cycle.inc\"";

	auto reader = [&files](const Path &path, std::string *out) -> bool {
		auto it = files.find(path.ToString());
		if (it == files.end()) return false;
		*out = it->second;
		return true;
	};

	// Test 1: single include inlined + directive removed
	std::string src1 = "#version 450\n#include \"common.inc\"\nvoid main() {}";
	std::string out1, err1;
	EXPECT_TRUE(ResolveSlangIncludes(src1, Path("/base"), reader, &out1, &err1));
	EXPECT_TRUE(out1.find("helper()") != std::string::npos);
	EXPECT_TRUE(out1.find("#include") == std::string::npos);

	// Test 2: nested include (a includes b)
	std::string src2 = "#include \"nested.inc\"\nvoid main() {}";
	std::string out2, err2;
	EXPECT_TRUE(ResolveSlangIncludes(src2, Path("/base"), reader, &out2, &err2));
	EXPECT_TRUE(out2.find("deep()") != std::string::npos);
	EXPECT_TRUE(out2.find("middle()") != std::string::npos);
	EXPECT_TRUE(out2.find("#include") == std::string::npos);

	// Test 3: #pragma include_optional missing -> empty, no error
	std::string src3 = "#pragma include_optional \"missing.inc\"\nvoid main() {}";
	std::string out3, err3;
	EXPECT_TRUE(ResolveSlangIncludes(src3, Path("/base"), reader, &out3, &err3));
	EXPECT_TRUE(out3.find("void main") != std::string::npos);
	EXPECT_TRUE(out3.find("#pragma include_optional") == std::string::npos);

	// Test 4: cycle or depth guard -> error
	std::string src4 = "#include \"cycle.inc\"";
	std::string out4, err4;
	EXPECT_FALSE(ResolveSlangIncludes(src4, Path("/base"), reader, &out4, &err4));
	EXPECT_TRUE(err4.find("recursion") != std::string::npos || err4.find("depth") != std::string::npos);

	// Test 5: #include missing (not optional) -> error
	std::string src5 = "#include \"notfound.inc\"";
	std::string out5, err5;
	EXPECT_FALSE(ResolveSlangIncludes(src5, Path("/base"), reader, &out5, &err5));

	return true;
}

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

