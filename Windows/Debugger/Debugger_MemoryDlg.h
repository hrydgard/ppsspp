#pragma once
#include "Windows/W32Util/DialogManager.h"

#include "Core/MemMap.h"

#include "Core/Debugger/DebugInterface.h"
#include "CtrlMemView.h"
#include "Common/CommonWindows.h"

class CMemoryDlg : public Dialog
{
private:
	DebugInterface *cpu;
	static RECT slRect;
	RECT winRect, srRect;
	CtrlMemView *memView;
	HWND memViewHdl, symListHdl, editWnd, searchBoxHdl, srcListHdl;
	HWND layerDropdown_;
	HWND status_;
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

public:
	int index; //helper 

	void searchBoxRedraw(const std::vector<u32> &results);

	// constructor
	CMemoryDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu);
	
	// destructor
	~CMemoryDlg(void);
	
	void Goto(u32 addr);
	void Update(void) override;
	void NotifyMapLoaded();

	void Size(void);

private:
	bool mapLoadPending_ = false;
};


