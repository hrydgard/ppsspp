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

	bool AttemptLoadSymbolMap() override;
	void SaveSymbolMap() override;

	void ToggleDebugConsoleVisibility() override;

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;

	GraphicsContext *GetGraphicsContext() { return nullptr; }

private:
	void SetConsolePosition();
	void UpdateConsolePosition();

	std::list<std::unique_ptr<InputDevice>> input;
};
