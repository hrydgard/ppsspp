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
