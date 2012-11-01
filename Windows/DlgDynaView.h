#pragma once


class CDynaViewDlg
{
private:
	static HINSTANCE		m_hInstance;
	static HWND				m_hParent;
	static int				m_iBlock;
	static HWND				m_hDlg;

	static BOOL CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

public:
	// constructor
	static void Init(HINSTANCE _hInstance, HWND _hParent);

	// destructor
	static void DeInit(void);
	//
	// --- tools ---
	//
	static HWND GetDlgHandle()
	{
		return m_hDlg;
	}

	// show
	static void Show(bool _bShow);
	static void Size();

	static void View(int num);
	static void ViewAddr(u32 addr);
};
