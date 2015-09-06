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