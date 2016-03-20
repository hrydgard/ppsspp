#include <string>
#include <stdio.h>

#include "util/text/parsers.h"

bool Version::ParseVersionString(std::string str) {
	if (str.empty())
		return false;
	if (str[0] == 'v')
		str = str.substr(1);
	if (3 != sscanf(str.c_str(), "%i.%i.%i", &major, &minor, &sub)) {
		sub = 0;
		if (2 != sscanf(str.c_str(), "%i.%i", &major, &minor))
			return false;
	}
	return true;
}

std::string Version::ToString() const {
	char temp[128];
	sprintf(temp, "%i.%i.%i", major, minor, sub);
	return std::string(temp);
}

int Version::ToInteger() const {
	// This allows for ~2000 major versions, ~100 minor versions, and ~10000 sub versions.
	return major * 1000000 + minor * 10000 + sub;
}

bool ParseMacAddress(std::string str, uint8_t macAddr[6]) {
	int mac[6];
	if (6 != sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5])) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		macAddr[i] = mac[i];
	}
	return true;
}