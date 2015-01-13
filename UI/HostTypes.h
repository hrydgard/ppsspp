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

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
#include "Core/Debugger/SymbolMap.h"
#include "Qt/mainwindow.h"
#endif

// TODO: Get rid of this junk
class NativeHost : public Host {
public:
	NativeHost() {
	}

	void UpdateUI() override {}

	void UpdateMemView() override {}
	void UpdateDisassembly() override {}

	void SetDebugMode(bool mode) override { }

	bool InitGraphics(std::string *error_message) override { return true; }
	void ShutdownGraphics() override {}

	void InitSound() override;
	void UpdateSound() override {}
	void ShutdownSound() override;

	// this is sent from EMU thread! Make sure that Host handles it properly!
	void BootDone() override {}

	bool IsDebuggingEnabled() override {return false;}
	bool AttemptLoadSymbolMap() override {return false;}
	void SetWindowTitle(const char *message) override {}
};

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
static const char* SymbolMapFilename(std::string currentFilename)
{
	std::string result = currentFilename;
	size_t dot = result.rfind('.');
	if (dot == result.npos)
		return (result + ".map").c_str();

	result.replace(dot, result.npos, ".map");
	return result.c_str();
}

class QtHost : public Host {
public:
	QtHost(MainWindow *mainWindow_)
	{
		mainWindow = mainWindow_;
		m_GPUStep = false;
		m_GPUFlag = 0;
	}

	virtual void UpdateUI() override {
		mainWindow->updateMenus();
	}

	virtual void UpdateMemView() override {
		if(mainWindow->GetDialogMemory())
			mainWindow->GetDialogMemory()->Update();
	}
	virtual void UpdateDisassembly() override {
		if(mainWindow->GetDialogDisasm())
			mainWindow->GetDialogDisasm()->Update();
		if(mainWindow->GetDialogDisplaylist())
			mainWindow->GetDialogDisplaylist()->Update();
	}

	virtual void SetDebugMode(bool mode) override {
		if(mainWindow->GetDialogDisasm())
			mainWindow->GetDialogDisasm()->SetDebugMode(mode);
	}

	virtual bool InitGraphics(std::string *error_message) override { return true; }
	virtual void ShutdownGraphics() override {}

	virtual void InitSound() override;
	virtual void UpdateSound() override {}
	virtual void ShutdownSound();

	// this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {
		symbolMap.SortSymbols();
		mainWindow->Boot();
	}

	virtual bool IsDebuggingEnabled() {
#ifdef _DEBUG
		return true;
#else
		return false;
#endif
	}
	virtual bool AttemptLoadSymbolMap() {
		return symbolMap.LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart));
	}
	virtual void PrepareShutdown() {
		symbolMap.SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart));
	}
	virtual void ResetSymbolMap() {}
	virtual void AddSymbol(std::string name, u32 addr, u32 size, int type=0) {}
	virtual void SetWindowTitle(const char *message) {
		QString title = "PPSSPP " + QString(PPSSPP_GIT_VERSION) + " - " + QString::fromUtf8(message);

		mainWindow->setWindowTitle(title);
	}
	bool GPUDebuggingActive()
	{
		auto dialogDisplayList = mainWindow->GetDialogDisplaylist();
		if (dialogDisplayList && dialogDisplayList->isVisible())
		{
			if (m_GPUStep && m_GPUFlag == -1)
				m_GPUFlag = 0;

			return true;
		}
		return false;
	}
	void SetGPUStep(bool value, int flag = 0, u32 data = 0)
	{
		m_GPUStep = value;
		m_GPUFlag = flag;
		m_GPUData = data;
	}
private:
	MainWindow* mainWindow;
	bool m_GPUStep;
	int m_GPUFlag;
	u32 m_GPUData;
};
#endif
