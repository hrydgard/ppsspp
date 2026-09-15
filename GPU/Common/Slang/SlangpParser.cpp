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
#include <map>
#include <sstream>
#include <functional>

#include "Common/StringUtils.h"
#include "GPU/Common/Slang/SlangpParser.h"

static std::string Unquote(std::string s) {
	std::string_view view = StripSpaces(s);
	if (view.size() >= 2 && view.front() == '"' && view.back() == '"')
		view = view.substr(1, view.size() - 2);
	return std::string(view);
}

static SlangScaleType ParseScaleType(const std::string &v) {
	if (v == "viewport") return SlangScaleType::Viewport;
	if (v == "absolute") return SlangScaleType::Absolute;
	return SlangScaleType::Source;  // default
}

static SlangWrapMode ParseWrapMode(const std::string &v) {
	if (v == "clamp_to_edge") return SlangWrapMode::ClampToEdge;
	if (v == "repeat") return SlangWrapMode::Repeat;
	if (v == "mirrored_repeat") return SlangWrapMode::MirroredRepeat;
	return SlangWrapMode::ClampToBorder;  // slang default
}

bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error) {
	out->basePath = basePath;
	out->passes.clear();
	out->values.clear();

	// key -> value map (last write wins, matching RetroArch).
	std::map<std::string, std::string> kv;
	std::stringstream ss(text);
	std::string line;
	while (std::getline(ss, line)) {
		// Strip comments, but only outside quotes.
		bool inQuote = false;
		size_t commentStart = std::string::npos;
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] == '"') inQuote = !inQuote;
			else if (line[i] == '#' && !inQuote) {
				commentStart = i;
				break;
			}
		}
		if (commentStart != std::string::npos) line = line.substr(0, commentStart);

		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = std::string(StripSpaces(line.substr(0, eq)));
		std::string val = Unquote(line.substr(eq + 1));
		if (!key.empty()) kv[key] = val;
	}

	// Keep the raw lines for GetPresetParameters(); a one-time copy per preset load.
	out->values = kv;

	auto it = kv.find("shaders");
	if (it == kv.end()) { *error = "missing 'shaders' count"; return false; }
	int count = atoi(it->second.c_str());
	if (count <= 0 || count > 64) { *error = "bad 'shaders' count"; return false; }

	auto getStr = [&](const std::string &k, std::string *v) -> bool {
		auto f = kv.find(k); if (f == kv.end()) return false; *v = f->second; return true;
	};

	for (int i = 0; i < count; i++) {
		SlangPassDesc pass;
		std::string s;
		std::string idx = std::to_string(i);
		if (!getStr("shader" + idx, &s)) { *error = "missing shader" + idx; return false; }
		pass.shaderPath = (basePath / s).ToString();

		std::string tmp;
		if (getStr("filter_linear" + idx, &tmp)) pass.filterLinear = (tmp == "true" || tmp == "1");
		if (getStr("alias" + idx, &tmp)) pass.alias = tmp;

		// scale_typeN sets both axes; per-axis overrides win.
		if (getStr("scale_type" + idx, &tmp)) { pass.scaleTypeX = pass.scaleTypeY = ParseScaleType(tmp); }
		if (getStr("scale_type_x" + idx, &tmp)) pass.scaleTypeX = ParseScaleType(tmp);
		if (getStr("scale_type_y" + idx, &tmp)) pass.scaleTypeY = ParseScaleType(tmp);
		if (getStr("scale" + idx, &tmp)) { pass.scaleX = pass.scaleY = (float)atof(tmp.c_str()); }
		if (getStr("scale_x" + idx, &tmp)) pass.scaleX = (float)atof(tmp.c_str());
		if (getStr("scale_y" + idx, &tmp)) pass.scaleY = (float)atof(tmp.c_str());

		if (getStr("srgb_framebuffer" + idx, &tmp)) pass.srgbFramebuffer = (tmp == "true" || tmp == "1");
		if (getStr("float_framebuffer" + idx, &tmp)) pass.floatFramebuffer = (tmp == "true" || tmp == "1");
		if (getStr("mipmap_input" + idx, &tmp)) pass.mipmapInput = (tmp == "true" || tmp == "1");
		if (getStr("wrap_mode" + idx, &tmp)) pass.wrapMode = ParseWrapMode(tmp);
		if (getStr("frame_count_mod" + idx, &tmp)) pass.frameCountMod = atoi(tmp.c_str());

		out->passes.push_back(pass);
	}

	std::string fp;
	if (getStr("feedback_pass", &fp)) out->feedbackPass = atoi(fp.c_str());

	// Parse LUT textures
	std::string texList;
	if (getStr("textures", &texList)) {
		std::vector<std::string> names;
		// split on ';', trim each
		size_t start = 0;
		while (start <= texList.size()) {
			size_t sep = texList.find(';', start);
			std::string nm = std::string(StripSpaces(texList.substr(start, sep == std::string::npos ? std::string::npos : sep - start)));
			if (!nm.empty()) {
				SlangLutDesc lut;
				lut.name = nm;
				std::string p;
				if (getStr(nm, &p)) lut.path = (basePath / p).ToString();
				std::string t;
				if (getStr(nm + "_linear", &t)) lut.linear = (t == "true" || t == "1");
				if (getStr(nm + "_mipmap", &t)) lut.mipmap = (t == "true" || t == "1");
				if (getStr(nm + "_wrap_mode", &t)) lut.wrapMode = ParseWrapMode(t);
				out->luts.push_back(lut);
			}
			if (sep == std::string::npos) break;
			start = sep + 1;
		}
	}

	return true;
}

// Helper to recursively resolve includes with depth guard
static bool ResolveSlangIncludesRecursive(const std::string &src, const Path &sourceDir,
                                          const SlangFileReader &reader, int depth,
                                          std::string *out, std::string *error) {
	if (depth > 32) {
		*error = "include recursion too deep (cycle or runaway)";
		return false;
	}

	out->clear();
	std::stringstream ss(src);
	std::string line;
	while (std::getline(ss, line)) {
		std::string trimmed = std::string(StripSpaces(line));
		bool isInclude = false;
		bool isOptional = false;
		std::string includePath;

		// Match: #include "path" or #pragma include "path" or #pragma include_optional "path"
		if (startsWith(trimmed, "#include")) {
			std::string rest = std::string(StripSpaces(trimmed.substr(strlen("#include"))));
			if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
				includePath = rest.substr(1, rest.size() - 2);
				isInclude = true;
			}
		} else if (startsWith(trimmed, "#pragma")) {
			std::string rest = std::string(StripSpaces(trimmed.substr(strlen("#pragma"))));
			if (startsWith(rest, "include_optional")) {
				std::string pathPart = std::string(StripSpaces(rest.substr(strlen("include_optional"))));
				if (pathPart.size() >= 2 && pathPart.front() == '"' && pathPart.back() == '"') {
					includePath = pathPart.substr(1, pathPart.size() - 2);
					isInclude = true;
					isOptional = true;
				}
			} else if (startsWith(rest, "include")) {
				std::string pathPart = std::string(StripSpaces(rest.substr(strlen("include"))));
				if (pathPart.size() >= 2 && pathPart.front() == '"' && pathPart.back() == '"') {
					includePath = pathPart.substr(1, pathPart.size() - 2);
					isInclude = true;
				}
			}
		}

		if (isInclude && !includePath.empty()) {
			// Resolve path relative to sourceDir
			Path fullPath = sourceDir / includePath;
			std::string includeContent;
			if (!reader(fullPath, &includeContent)) {
				if (!isOptional) {
					*error = "failed to read include: " + includePath;
					return false;
				}
				// Optional include missing -> substitute empty, continue
				continue;
			}

			// Recursively resolve includes in the included file
			Path includeDir = Path(fullPath.GetDirectory());
			std::string resolved;
			if (!ResolveSlangIncludesRecursive(includeContent, includeDir, reader, depth + 1, &resolved, error)) {
				return false;
			}

			// Substitute the resolved content (without the directive line itself)
			*out += resolved;
		} else {
			// Non-include line: pass through
			*out += line + "\n";
		}
	}

	return true;
}

bool ResolveSlangIncludes(const std::string &src, const Path &sourceDir, const SlangFileReader &reader,
                          std::string *out, std::string *error) {
	return ResolveSlangIncludesRecursive(src, sourceDir, reader, 0, out, error);
}

// Parse: #pragma parameter NAME "Description" INIT MIN MAX [STEP]
static bool ParseParameterPragma(const std::string &rest, SlangParamDesc *p) {
	// rest is everything after "#pragma parameter ".
	size_t q1 = rest.find('"');
	size_t q2 = (q1 == std::string::npos) ? std::string::npos : rest.find('"', q1 + 1);
	if (q1 == std::string::npos || q2 == std::string::npos) return false;
	p->name = std::string(StripSpaces(rest.substr(0, q1)));
	p->description = rest.substr(q1 + 1, q2 - q1 - 1);   // text between the quotes
	std::string tail = rest.substr(q2 + 1);  // " INIT MIN MAX [STEP]"
	std::istringstream nums(tail);
	nums >> p->initial >> p->minimum >> p->maximum;
	if (!(nums >> p->step)) p->step = 0.0f;
	return !p->name.empty();
}

bool SplitSlangSource(const std::string &src, SlangSource *out, std::string *error) {
	out->vertex.clear();
	out->fragment.clear();
	out->name.clear();
	out->params.clear();
	out->format = SlangFbFormat::Default;

	std::string prologue;
	// stage: 0 = prologue (shared), 1 = vertex, 2 = fragment
	int stage = 0;
	std::stringstream ss(src);
	std::string line;
	while (std::getline(ss, line)) {
		std::string trimmed = std::string(StripSpaces(line));
		if (startsWith(trimmed, "#pragma")) {
			std::string rest = std::string(StripSpaces(trimmed.substr(strlen("#pragma"))));
			if (startsWith(rest, "stage")) {
				std::string st = std::string(StripSpaces(rest.substr(strlen("stage"))));
				stage = (st == "vertex") ? 1 : (st == "fragment") ? 2 : stage;
				continue;
			} else if (startsWith(rest, "name")) {
				out->name = std::string(StripSpaces(rest.substr(strlen("name"))));
				continue;
			} else if (startsWith(rest, "parameter")) {
				SlangParamDesc p;
				if (ParseParameterPragma(std::string(StripSpaces(rest.substr(strlen("parameter")))), &p))
					out->params.push_back(p);
				continue;
			} else if (startsWith(rest, "format")) {
				std::string fmt = std::string(StripSpaces(rest.substr(strlen("format"))));
				if (fmt.find("_SFLOAT") != std::string::npos) out->format = SlangFbFormat::Float;
				else if (fmt.find("_SRGB") != std::string::npos) out->format = SlangFbFormat::Srgb;
				else out->format = SlangFbFormat::Default;
				continue;
			}
			// Unknown pragma: fall through and emit it.
		}
		if (stage == 0) prologue += line + "\n";
		else if (stage == 1) out->vertex += line + "\n";
		else out->fragment += line + "\n";
	}
	out->vertex = prologue + out->vertex;
	out->fragment = prologue + out->fragment;
	return true;
}
