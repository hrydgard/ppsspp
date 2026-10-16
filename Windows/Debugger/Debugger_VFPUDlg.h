#pragma once

#include "Windows/W32Util/DialogManager.h"
#include "Core/MemMap.h"
#include "Core/Debugger/DebugInterface.h"

class CVFPUDlg : public Dialog {
public:
	CVFPUDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu_);
	~CVFPUDlg();

	void Update() override;

private:
	void ComputeLayout();
	void Paint(HDC hdc);
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

	// In pixels, derived from the font.
	struct Layout {
		int rowHeight;
		int columnWidth;
		int xStart;
		int yStart;
		int pad;
	};

	DebugInterface *cpu;
	HFONT font;
	int mode;
	Layout layout_{};
};
