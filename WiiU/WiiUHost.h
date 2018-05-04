#pragma once

#include "../Core/Host.h"
#include "WiiU/GX2GraphicsContext.h"

class GraphicsContext;

class WiiUHost : public Host {
public:
	WiiUHost();
	~WiiUHost();

	bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override;
	void ShutdownGraphics() override;

	void InitSound() override;
	void ShutdownSound() override;

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;
	void SendUIMessage(const std::string &message, const std::string &value) override;
private:
	GX2GraphicsContext ctx_;
	int InitSoundRefCount_ = 0;
};
