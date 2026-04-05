#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Common/CommonWindows.h"

namespace W32Util {
	void CenterWindow(HWND hwnd);
	bool CopyTextToClipboard(HWND hwnd, std::string_view text);
	void MakeTopMost(HWND hwnd, bool topMost);
	void ExitAndRestart(bool overrideArgs = false, const std::string &args = "");
	void SpawnNewInstance(bool overrideArgs = false, const std::string &args = "");
	bool ExecuteAndGetReturnCode(const wchar_t *executable, const wchar_t *cmdline, const wchar_t *currentDirectory, DWORD *exitCode);
	void GetSelfExecuteParams(std::wstring &workingDirectory, std::wstring &moduleFilename);

	void GetWindowRes(HWND hWnd, int *xres, int *yres);
	void ShowFileInFolder(std::string_view path);
	RECT GetNonclientMenuBorderRect(HWND hwnd);

	struct ClipboardData {
		ClipboardData(const char *format, size_t sz);
		ClipboardData(UINT format, size_t sz);
		~ClipboardData();

		void Set();

		operator bool() {
			return data != nullptr;
		}

		UINT format_;
		HANDLE handle_;
		void *data;
	};
}

struct GenericListViewColumn
{
	const wchar_t *name;
	float size;
	int flags;
};

struct GenericListViewDef
{
	const GenericListViewColumn* columns;
	int columnCount;
	int* columnOrder;
	bool checkbox;			// the first column will always have the checkbox. specify a custom order to change its position
};

#define GLVC_CENTERED		1

typedef struct tagNMLVCUSTOMDRAW *LPNMLVCUSTOMDRAW;

// the most significant bit states whether the key is currently down.
// simply checking if it's != 0 is not enough, as bit0 is set if
// the key was pressed between the last call to GetAsyncKeyState
bool KeyDownAsync(int vkey);

class GenericListControl
{
public:
	GenericListControl(HWND hwnd, const GenericListViewDef& def);
	virtual ~GenericListControl();
	int HandleNotify(LPARAM lParam);
	void Update();
	int GetSelectedIndex();
	HWND GetHandle() { return handle; };
	void SetSendInvalidRows(bool enabled) { sendInvalidRows = enabled; };
protected:
	void SetIconList(int w, int h, const std::vector<HICON> &icons);
	void SetCheckState(int item, bool state);
	void SetItemState(int item, uint8_t state);

	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue) = 0;
	virtual void GetColumnText(wchar_t* dest, size_t destSize, int row, int col) = 0;
	virtual int GetRowCount() = 0;
	virtual void OnDoubleClick(int itemIndex, int column) { };
	virtual void OnRightClick(int itemIndex, int column, const POINT& point) { };
	virtual void CopyRows(int start, int size);
	virtual void OnToggle(int item, bool newValue) { };

	virtual bool ListenRowPrePaint() { return false; }
	virtual bool ListenColPrePaint() { return false; }
	virtual bool OnRowPrePaint(int row, LPNMLVCUSTOMDRAW msg) { return false; }
	virtual bool OnColPrePaint(int row, int col, LPNMLVCUSTOMDRAW msg) { return false; }

	virtual int OnIncrementalSearch(int startRow, const wchar_t *str, bool wrap, bool partial);

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ProcessUpdate();
	void ResizeColumns();
	void ProcessCopy();
	void SelectAll();

	HWND handle;
	WNDPROC oldProc;
	void *images_ = nullptr;
	const GenericListViewColumn* columns;
	wchar_t stringBuffer[256];  // needs to survive slightly longer than the GetColumnText scope, so needs to be here.
	int columnCount;
	bool valid;
	bool sendInvalidRows;
	// Used for hacky workaround to fix a rare hang (see issue #5184)
	volatile bool inResizeColumns;
	volatile bool updating;
	bool updateScheduled_ = false;

	enum class Action {
		CHECK,
		IMAGE,
	};
	struct PendingAction {
		Action action;
		int item;
		int state;
	};
	std::vector<PendingAction> pendingActions_;
};
