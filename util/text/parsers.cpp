#include <string>
#include <stdio.h>

#include "util/text/parsers.h"

bool Version::ParseVersionString(std::string str) {
	if (str.empty())
		return false;
	if (str[0] == 'v')
		str = str.substr(1);
	return 3 == sscanf(str.c_str(), "%i.%i.%i", &major, &minor, &sub);
}

std::string Version::ToString() const {
	char temp[128];
	sprintf(temp, "%i.%i.%i", major, minor, sub);
	return std::string(temp);
}
