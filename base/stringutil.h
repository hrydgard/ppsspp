#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string>

class ITOA {
public:
	char buffer[16];
	const char *p(int i) {
		sprintf(buffer, "%i", i);
		return &buffer[0];
	}
};
	
inline bool endsWith(const std::string &str, const std::string &what) {
	return str.substr(str.size() - what.size()) == what;
}