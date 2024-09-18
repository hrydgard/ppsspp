#pragma once

// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include <atomic>

#include "Common/Log/LogManager.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif

class StdioListener : public LogListener {
public:
	StdioListener();
	void Log(const LogMessage &message) override;
private:
	bool bUseColor = true;
};
