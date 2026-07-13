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

bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error) {
	out->basePath = basePath;
	out->passes.clear();
	out->params.clear();

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

		out->passes.push_back(pass);
	}
	return true;
}

// Parse: #pragma parameter NAME "Description" INIT MIN MAX [STEP]
static bool ParseParameterPragma(const std::string &rest, SlangParamDesc *p) {
	// rest is everything after "#pragma parameter ".
	size_t q1 = rest.find('"');
	size_t q2 = (q1 == std::string::npos) ? std::string::npos : rest.find('"', q1 + 1);
	if (q1 == std::string::npos || q2 == std::string::npos) return false;
	p->name = std::string(StripSpaces(rest.substr(0, q1)));
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
				continue;  // consumed; Phase 1 uses default RT format
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
