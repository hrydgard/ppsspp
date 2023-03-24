#pragma once

#include "Core/Host.h"

#include <list>
#include <memory>

#include "Windows/InputDevice.h"

class UWPHost : public Host {
public:
	UWPHost();
	~UWPHost();

	void PollControllers() override;

	void UpdateSound() override;

	void ToggleDebugConsoleVisibility() override;

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;

private:
	std::list<std::unique_ptr<InputDevice>> input;
};
