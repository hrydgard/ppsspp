#include "TabControl.h"
#include "DialogManager.h"
#include "Windows/MainWindow.h"
#include <windowsx.h>
#include <commctrl.h>

const DWORD tabControlStyleMask = ~(WS_POPUP | WS_TILEDWINDOW);

TabControl::TabControl(HWND handle, bool noDisplayArea)
	: hwnd(handle), noDisplayArea_(noDisplayArea)
{
	SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)this);
	oldProc = (WNDPROC) SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)wndProc);

	hasButtons = (GetWindowLong(handle,GWL_STYLE) & TCS_BUTTONS) != 0;
}

HWND TabControl::AddTabWindow(const wchar_t* className, const wchar_t *title, DWORD style)
{
	TabInfo info;
	info.hasBorder = (style & WS_BORDER) != 0;

	style = (style |WS_CHILD) & tabControlStyleMask;
	if (showTabTitles)
		AppendPageToControl(title);
	int index = (int)tabs.size();

	RECT tabRect;
	GetWindowRect(hwnd,&tabRect);
	MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);
	TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);

	HWND tabHandle = CreateWindowEx(0,className,title,style,
		tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,
		GetParent(hwnd),0,MainWindow::GetHInstance(),0);
	
	info.hasClientEdge = (GetWindowLong(tabHandle,GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;
	if (hasButtons == false)
	{
		SetWindowLong(tabHandle, GWL_STYLE, GetWindowLong(tabHandle,GWL_STYLE) & (~WS_BORDER));
		SetWindowLong(tabHandle, GWL_EXSTYLE, GetWindowLong(tabHandle,GWL_EXSTYLE) & (~WS_EX_CLIENTEDGE));
	}

	info.lastFocus = tabHandle;
	info.pageHandle = tabHandle;
	wcscpy_s(info.title,title);
	tabs.push_back(info);

	ShowTab(index);
	return tabHandle;
}

void TabControl::AddTabDialog(Dialog* dialog, const wchar_t* title)
{
	HWND handle = dialog->GetDlgHandle();
	AddTab(handle,title);
}

void TabControl::AddTab(HWND handle, const wchar_t* title)
{
	if (showTabTitles)
		AppendPageToControl(title);
	int index = (int)tabs.size();
	
	TabInfo info = {0};
	if (!noDisplayArea_)
	{
		RECT tabRect;
		GetWindowRect(hwnd,&tabRect);
		MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);
		TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);

		SetParent(handle,GetParent(hwnd));
		DWORD style = (GetWindowLong(handle,GWL_STYLE) | WS_CHILD);

		info.hasBorder = (style & WS_BORDER) != 0;
		info.hasClientEdge = (GetWindowLong(handle,GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;
		if (hasButtons == false)
		{
			style &= (~WS_BORDER);
			SetWindowLong(handle, GWL_EXSTYLE, GetWindowLong(handle,GWL_EXSTYLE) & (~WS_EX_CLIENTEDGE));
		}

		SetWindowLong(handle, GWL_STYLE, style & tabControlStyleMask);
		MoveWindow(handle,tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,TRUE);
	}

	info.lastFocus = handle;
	info.pageHandle = handle;
	wcscpy_s(info.title,title);
	tabs.push_back(info);

	ShowTab(index);
}

HWND TabControl::RemoveTab(int index) {
	int prevIndex = CurrentTabIndex();
	if (currentTab >= index)
		--currentTab;

	HWND prevHandle = tabs[index].pageHandle;
	if (tabs.size() == 1) {
		TabCtrl_DeleteAllItems(hwnd);
		tabs.clear();
		currentTab = 0;
	} else {
		TabCtrl_DeleteItem(hwnd, index);
		tabs.erase(tabs.begin() + index);

		if (prevIndex == index)
			ShowTab(currentTab, true);
	}

	return prevHandle;
}

int TabControl::AppendPageToControl(const wchar_t *title)
{
	TCITEM tcItem;
	ZeroMemory (&tcItem,sizeof (tcItem));
	tcItem.mask			= TCIF_TEXT;
	tcItem.dwState		= 0;
	tcItem.pszText		= (LPTSTR)title;
	tcItem.cchTextMax	= (int)wcslen(tcItem.pszText)+1;
	tcItem.iImage		= 0;

	int index = TabCtrl_GetItemCount(hwnd);
	TabCtrl_InsertItem(hwnd, index, &tcItem);
	return index;
}

bool OffspringHasFocus(HWND handle)
{
	HWND offspring = GetFocus();
	HWND start = offspring;

	while (offspring != NULL)
	{
		if (offspring == handle) return true;
		offspring = GetParent(offspring);

		// no idea if this can potentially go in circles, make sure to stop just in case
		if (offspring == start) break;
	}

	return false;
}

void TabControl::ShowTab(int index, bool setControlIndex)
{
	bool oldFocus = OffspringHasFocus(CurrentTabHandle());
	if (oldFocus)
		tabs[CurrentTabIndex()].lastFocus = GetFocus();

	currentTab = index;

	for (size_t i = 0; i < tabs.size(); i++)
	{
		if (oldFocus && i == index)
			SetFocus(tabs[i].lastFocus);
		if (!noDisplayArea_)
			ShowWindow(tabs[i].pageHandle,i == index ? SW_NORMAL : SW_HIDE);
	}

	if (setControlIndex && showTabTitles)
		TabCtrl_SetCurSel(hwnd,index);
}

void TabControl::ShowTab(HWND pageHandle)
{
	bool oldFocus = OffspringHasFocus(CurrentTabHandle());
	if (oldFocus)
		tabs[CurrentTabIndex()].lastFocus = GetFocus();

	for (size_t i = 0; i < tabs.size(); i++)
	{
		if (tabs[i].pageHandle == pageHandle)
		{
			currentTab = (int)i;
			if (showTabTitles)
				TabCtrl_SetCurSel(hwnd,i);
			if (oldFocus)
				SetFocus(tabs[i].lastFocus);
		}
		if (!noDisplayArea_)
			ShowWindow(tabs[i].pageHandle,tabs[i].pageHandle == pageHandle ? SW_NORMAL : SW_HIDE);
	}
}

void TabControl::SetShowTabTitles(bool enabled)
{
	showTabTitles = enabled;
	int itemCount = TabCtrl_GetItemCount(hwnd);

	for (int i = 0; i < itemCount; i++)
	{
		TabCtrl_DeleteItem(hwnd,0);
	}

	if (showTabTitles)
	{
		for (int i = 0; i < (int) tabs.size(); i++)
		{
			AppendPageToControl(tabs[i].title);
			
			if (hasButtons == false && !noDisplayArea_)
			{
				DWORD style = GetWindowLong(tabs[i].pageHandle,GWL_STYLE) & (~WS_BORDER);
				SetWindowLong(tabs[i].pageHandle,GWL_STYLE,style);

				DWORD exStyle = GetWindowLong(tabs[i].pageHandle,GWL_EXSTYLE) & (~WS_EX_CLIENTEDGE);
				SetWindowLong(tabs[i].pageHandle,GWL_EXSTYLE,exStyle);
			}
		}
		TabCtrl_SetCurSel(hwnd,CurrentTabIndex());
	} else if (hasButtons == false && !noDisplayArea_)
	{
		for (int i = 0; i < (int) tabs.size(); i++)
		{
			if (tabs[i].hasBorder)
			{
				DWORD style = GetWindowLong(tabs[i].pageHandle,GWL_STYLE) | WS_BORDER;
				SetWindowLong(tabs[i].pageHandle,GWL_STYLE,style);
			}

			if (tabs[i].hasClientEdge)
			{
				DWORD exStyle = GetWindowLong(tabs[i].pageHandle,GWL_EXSTYLE) | WS_EX_CLIENTEDGE;
				SetWindowLong(tabs[i].pageHandle,GWL_EXSTYLE,exStyle);
			}
		}
	}
	
	OnResize();
}

void TabControl::SetMinTabWidth(int w)
{
	TabCtrl_SetMinTabWidth(hwnd, w);
}

void TabControl::NextTab(bool cycle)
{
	int index = CurrentTabIndex()+1;
	if (index == tabs.size())
	{
		if (cycle == false)
			index--;
		else
			index = 0;
	}

	ShowTab(index);
}

void TabControl::PreviousTab(bool cycle)
{
	int index = CurrentTabIndex()-1;
	if (index < 0)
	{
		if (cycle == false)
			index = 0;
		else
			index = (int) tabs.size()-1;
	}

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

int TabControl::HitTest(const POINT &screenPos) {
	TCHITTESTINFO hitTest{};
	hitTest.pt = screenPos;
	ScreenToClient(hwnd, &hitTest.pt);

	return TabCtrl_HitTest(hwnd, &hitTest);
}

void TabControl::OnResize()
{
	RECT tabRect;
	GetWindowRect(hwnd,&tabRect);
	MapWindowPoints(HWND_DESKTOP,GetParent(hwnd),(LPPOINT)&tabRect,2);

	InvalidateRect(hwnd,NULL,TRUE);
	UpdateWindow(hwnd);
	
	// now resize tab children
	if (showTabTitles)
	{
		int bottom = tabRect.bottom;
		TabCtrl_AdjustRect(hwnd, FALSE, &tabRect);
		if (ignoreBottomMargin) tabRect.bottom = bottom;
	}

	int current = CurrentTabIndex();

	if (!noDisplayArea_)
	{
		for (size_t i = 0; i < tabs.size(); i++)
		{
			InvalidateRect(tabs[i].pageHandle,NULL,FALSE);
			MoveWindow(tabs[i].pageHandle,tabRect.left,tabRect.top,tabRect.right-tabRect.left,tabRect.bottom-tabRect.top,TRUE);

			if (i == current)
			{
				InvalidateRect(tabs[i].pageHandle,NULL,TRUE);
				UpdateWindow(tabs[i].pageHandle);
			}
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
