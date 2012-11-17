#pragma once

#include <windows.h>

class Dialog
{
protected:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	HWND m_hDlg;
	LPCSTR m_hResource;

	virtual BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) 
	{
		MessageBox(0,"WTF? Pure Call",0,0);	
		return 0;
	}
	static INT_PTR CALLBACK DlgProcStatic(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	virtual void Create();
	void Destroy();
public:
	Dialog(LPCSTR res, HINSTANCE _hInstance, HWND _hParent);
	virtual ~Dialog();
	void Show(bool _bShow);

	virtual void Update() {}

	HWND GetDlgHandle()
	{
		return m_hDlg;
	}
};


class DialogManager
{
public:
	static void AddDlg(Dialog *dialog);
	static bool IsDialogMessage(LPMSG message);
	static void EnableAll(BOOL enable);
	static void DestroyAll();
	static void UpdateAll();
};