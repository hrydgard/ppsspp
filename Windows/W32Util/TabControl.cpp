#include "TabControl.h"
#include "DialogManager.h"
#include "Windows/WndMainWindow.h"
#include <windowsx.h>
#include <commctrl.h>

TabControl::TabControl(HWND handle): hwnd(handle)
{
	SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)this);
	oldProc = (WNDPROC) SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)wndProc);

}

HWND TabControl::AddTabWindow(wchar_t* className, wchar_t* title, DWORD style)
{
	style |= WS_CHILD;

	TCITEM tcItem;
	ZeroMemory (&tcItem,sizeof (tcItem));
	tcItem.mask			= TCIF_TEXT;
	tcItem.dwState		= 0;
	tcItem.pszText		= title;
	tcItem.cchTextMax	= (int)wcslen(tcItem.pszText)+1;
	tcItem.iImage		= 0;

	int index = TabCtrl_GetItemCount(hwnd);
	int result = TabCtrl_InsertItem(hwnd,index,&tcItem);

	RECT tabRect;
	GetWindowRect(hwnd,&tabRect);
	MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);
	TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);

	HWND tabHandle = CreateWindowEx(0,className,title,style,
		tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,
		GetParent(hwnd),0,MainWindow::GetHInstance(),0);
	tabs.push_back(tabHandle);

	ShowTab(index);
	return tabHandle;
}

void TabControl::AddTabDialog(Dialog* dialog, wchar_t* title)
{
	HWND handle = dialog->GetDlgHandle();

	TCITEM tcItem;
	ZeroMemory (&tcItem,sizeof (tcItem));
	tcItem.mask			= TCIF_TEXT;
	tcItem.dwState		= 0;
	tcItem.pszText		= title;
	tcItem.cchTextMax	= (int)wcslen(tcItem.pszText)+1;
	tcItem.iImage		= 0;

	int index = TabCtrl_GetItemCount(hwnd);
	int result = TabCtrl_InsertItem(hwnd,index,&tcItem);

	RECT tabRect;
	GetWindowRect(hwnd,&tabRect);
	MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);
	TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);
	
	SetParent(handle,GetParent(hwnd));
	DWORD style = (GetWindowLong(handle,GWL_STYLE) | WS_CHILD) & ~(WS_POPUP | WS_TILEDWINDOW);
	SetWindowLong(handle, GWL_STYLE, style);
	MoveWindow(handle,tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,TRUE);
	tabs.push_back(handle);

	ShowTab(index);
}

void TabControl::ShowTab(int index, bool setControlIndex)
{
	for (size_t i = 0; i < tabs.size(); i++)
	{
		ShowWindow(tabs[i],i == index ? SW_NORMAL : SW_HIDE);
	}

	if (setControlIndex)
	{
		TabCtrl_SetCurSel(hwnd,index);
	}
}

void TabControl::ShowTab(HWND pageHandle)
{
	for (size_t i = 0; i < tabs.size(); i++)
	{
		if (tabs[i] == pageHandle)
		{
			TabCtrl_SetCurSel(hwnd,i);
		}
		ShowWindow(tabs[i],tabs[i] == pageHandle ? SW_NORMAL : SW_HIDE);
	}
}

void TabControl::NextTab(bool cycle)
{
	int index = TabCtrl_GetCurSel(hwnd);
	if (index == tabs.size()-1 && cycle)
		index = 0;
	ShowTab(index);
}

void TabControl::PreviousTab(bool cycle)
{
	int index = TabCtrl_GetCurSel(hwnd);
	if (index == 0 && cycle)
		index = (int) tabs.size()-1;
	ShowTab(index);
}

void TabControl::HandleNotify(LPARAM lParam)
{
	NMHDR* pNotifyMessage = NULL;
	pNotifyMessage = (LPNMHDR)lParam; 
	if (pNotifyMessage->hwndFrom == hwnd)
	{
		int iPage = TabCtrl_GetCurSel(hwnd);
		ShowTab(iPage,false);
	}
}

void TabControl::OnResize()
{
	RECT tabRect;
	GetWindowRect(hwnd,&tabRect);
	MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);

	InvalidateRect(hwnd,NULL,TRUE);
	UpdateWindow(hwnd);
	
	// now resize tab children
	TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);
	int current = TabCtrl_GetCurSel(hwnd);
	
	for (size_t i = 0; i < tabs.size(); i++)
	{
		InvalidateRect(tabs[i],NULL,FALSE);
		MoveWindow(tabs[i],tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,TRUE);

		if (i == current)
		{
			InvalidateRect(tabs[i],NULL,TRUE);
			UpdateWindow(tabs[i]);
		}
	}
}

LRESULT CALLBACK TabControl::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TabControl* tabControl = (TabControl*) GetWindowLongPtr(hwnd,GWLP_USERDATA);

	switch (msg)
	{
	case WM_SIZE:
		tabControl->OnResize();
		break;
	}

	return (LRESULT)CallWindowProc((WNDPROC)tabControl->oldProc,hwnd,msg,wParam,lParam);
}