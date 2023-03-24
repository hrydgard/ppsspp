#pragma once

#include "Core/Host.h"

#include <string>

class UWPHost : public Host {
public:
	UWPHost();
	~UWPHost();

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;
};
