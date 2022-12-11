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
	void setDisplayList(const DisplayList &_list) {
		list = _list;
		Update();
	}
protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override { return false; }
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return list.stackptr; }
	void OnDoubleClick(int itemIndex, int column) override;
private:
	DisplayList list;
};

class CtrlAllDisplayLists: public GenericListControl
{
public:
	CtrlAllDisplayLists(HWND hwnd);
	void setDisplayLists(const std::vector<DisplayList> &_lists) {
		lists = _lists;
		Update();
	}
protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return (int) lists.size(); }
	void OnDoubleClick(int itemIndex, int column) override;
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
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;
private:
	void UpdateSize(WORD width, WORD height);

	CtrlDisplayListView* displayList;
	CtrlDisplayListStack* stack;
	CtrlAllDisplayLists* allLists;
	std::vector<DisplayList> lists;
	int activeList;
};
