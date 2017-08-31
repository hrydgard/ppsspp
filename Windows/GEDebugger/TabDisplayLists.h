#pragma once

#include "Common/CommonWindows.h"
#include "Windows/resource.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/Misc.h"
#include "GPU/Common/GPUDebugInterface.h"

class CtrlDisplayListView;

class CtrlDisplayListStack: public GenericListControl
{
public:
	CtrlDisplayListStack(HWND hwnd);
	void setDisplayList(DisplayList& _list) { list = _list; Update(); }
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue) { return false; };
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return list.stackptr; };
	virtual void OnDoubleClick(int itemIndex, int column);
private:
	DisplayList list;
};

class CtrlAllDisplayLists: public GenericListControl
{
public:
	CtrlAllDisplayLists(HWND hwnd);
	void setDisplayLists(std::vector<DisplayList>& _lists) { lists = _lists; Update(); };
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue);
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return (int) lists.size(); };
	virtual void OnDoubleClick(int itemIndex, int column);
private:
	std::vector<DisplayList> lists;
};

class TabDisplayLists : public Dialog
{
public:
	TabDisplayLists(HINSTANCE _hInstance, HWND _hParent);
	~TabDisplayLists();
	void Update(bool reload = true);
protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
private:
	void UpdateSize(WORD width, WORD height);

	CtrlDisplayListView* displayList;
	CtrlDisplayListStack* stack;
	CtrlAllDisplayLists* allLists;
	std::vector<DisplayList> lists;
	int activeList;
};
