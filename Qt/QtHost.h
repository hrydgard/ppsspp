// Copyright (c) 2014- PPSSPP Project.

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

#pragma once

#include "Core/Host.h"
#include "UI/OnScreenDisplay.h"

#include "Core/Debugger/SymbolMap.h"
#include "Qt/mainwindow.h"

class QtHost : public Host {
public:
	QtHost(MainWindow *mainWindow_)
	{
		mainWindow = mainWindow_;
	}

	virtual void UpdateUI() override {
		mainWindow->updateMenus();
	}

	virtual void UpdateMemView() override {
	}
	virtual void UpdateDisassembly() override {
		mainWindow->updateMenus();
	}

	virtual void SetDebugMode(bool mode) override {
	}

	virtual bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override { return true; }
	virtual void ShutdownGraphics() override {}

	virtual void InitSound() override;
	virtual void UpdateSound() override {}
	virtual void ShutdownSound() override;

	// this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() override {
		g_symbolMap->SortSymbols();
		mainWindow->Notify(MainWindowMsg::BOOT_DONE);
	}

	virtual bool IsDebuggingEnabled() override {
#ifdef _DEBUG
		return true;
#else
		return false;
#endif
	}
	virtual bool AttemptLoadSymbolMap() override {
		return false;
		// TODO: Make this work with Qt and threaded GL... not sure what's so broken.
		// auto fn = SymbolMapFilename(PSP_CoreParameter().fileToStart);
		// return g_symbolMap->LoadSymbolMap(fn);
	}
	void PrepareShutdown() {
		g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart));
	}
	void SetWindowTitle(const char *message) override {
		std::string title = std::string("PPSSPP ") + PPSSPP_GIT_VERSION;
		if (message)
			title += std::string(" - ") + message;
#ifdef _DEBUG
		title += " (debug)";
#endif
		mainWindow->SetWindowTitleAsync(title);
	}

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override {
		osm.Show(message, duration, color, -1, true, id);
	}

	void SendUIMessage(const std::string &message, const std::string &value) override {
		NativeMessageReceived(message.c_str(), value.c_str());
	}

	bool GPUDebuggingActive() override {
		return false;
	}
private:
	const char* SymbolMapFilename(std::string currentFilename);
	MainWindow* mainWindow;
};
