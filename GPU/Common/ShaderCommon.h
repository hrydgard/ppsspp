// Copyright (c) 2015- PPSSPP Project.

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

enum DebugShaderType {
	SHADER_TYPE_VERTEX = 0,
	SHADER_TYPE_FRAGMENT = 1,
	SHADER_TYPE_GEOMETRY = 2,
	SHADER_TYPE_VERTEXLOADER = 3,  // Not really a shader, but might as well re-use this mechanism
	SHADER_TYPE_PIPELINE = 4,  // Vulkan and DX12 combines a bunch of state into pipeline objects. Might as well make them inspectable.
};

enum DebugShaderStringType {
	SHADER_STRING_SHORT_DESC = 0,
	SHADER_STRING_SOURCE_CODE = 1,
	SHADER_STRING_STATS = 2,
};
