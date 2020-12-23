#pragma once

// PPSSPP Python Debugging Interface
// Copyright (c) 2020 Thomas Perl <m@thp.io>.

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

struct PPSSPPPythonScripting {
    static void init();
    static void deinit();
    static void run_file(const std::string &filename);
    static void frame_hook();
    static std::string eval(const std::string &expr);
};
