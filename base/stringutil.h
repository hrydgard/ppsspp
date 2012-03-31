#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string>

// Dumb wrapper around itoa, providing a buffer. Declare this on the stack.
class ITOA {
public:
	char buffer[16];
	const char *p(int i) {
		sprintf(buffer, "%i", i);
		return &buffer[0];
	}
};

// Other simple string utilities.

inline bool endsWith(const std::string &str, const std::string &what) {
	return str.substr(str.size() - what.size()) == what;
}

// highly unsafe and not recommended.
unsigned int parseHex(const char* _szValue);
