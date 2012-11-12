#include "../Core/Host.h"
#include "InputDevice.h"
#include <list>
#include <memory>

class WindowsHost : public Host
{
public:
	WindowsHost(HWND _displayWindow)
	{
		displayWindow = _displayWindow;
		input = getInputDevices();
	}
	void UpdateMemView();
	void UpdateDisassembly();
	void UpdateUI();
	void SetDebugMode(bool mode);

	void AddSymbol(std::string name, u32 addr, u32 size, int type);

	void InitGL();
	void BeginFrame();
	void EndFrame();
	void ShutdownGL();

	void InitSound(PMixer *mixer);
	void UpdateSound();
	void ShutdownSound();

	bool IsDebuggingEnabled();
	void BootDone();
	void PrepareShutdown();
	bool AttemptLoadSymbolMap();

private:
	HWND displayWindow;
	std::list<std::shared_ptr<InputDevice>> input;
};