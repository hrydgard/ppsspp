// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <thread>
#include <mutex>

#include "Core/Dialog/PSPDialog.h"
#include "Core/Dialog/SavedataParam.h"

class PSPSaveDialog : public PSPDialog {
public:
	PSPSaveDialog(UtilityDialogType type);
	~PSPSaveDialog();

	int Init(int paramAddr);
	int Update(int animSpeed) override;
	int Shutdown(bool force = false) override;
	void DoState(PointerWrap &p) override;
	pspUtilityDialogCommon *GetCommonParam() override;

	void ExecuteIOAction();

protected:
	bool UseAutoStatus() override {
		return false;
	}

private:
	void DisplayBanner(int which);
	void DisplaySaveList(bool canMove = true);
	void DisplaySaveIcon(bool checkExists);
	void DisplaySaveDataInfo1();
	void DisplaySaveDataInfo2(bool showNewData = false);
	void DisplayMessage(std::string_view text, bool hasYesNo = false);
	std::string GetSelectedSaveDirName() const;

	void JoinIOThread();
	void StartIOThread();
	void ExecuteNotVisibleIOAction();

	enum DisplayState {
		DS_NONE,

		DS_SAVE_LIST_CHOICE,
		DS_SAVE_CONFIRM,
		DS_SAVE_CONFIRM_OVERWRITE,
		DS_SAVE_SAVING,
		DS_SAVE_DONE,

		DS_LOAD_LIST_CHOICE,
		DS_LOAD_CONFIRM,
		DS_LOAD_LOADING,
		DS_LOAD_DONE,
		DS_LOAD_NODATA,

		DS_DELETE_LIST_CHOICE,
		DS_DELETE_CONFIRM,
		DS_DELETE_DELETING,
		DS_DELETE_DONE,
		DS_DELETE_NODATA,

		DS_SAVE_FAILED,
		DS_LOAD_FAILED,
		DS_DELETE_FAILED,
	};

	enum DialogBanner {
		DB_NONE,
		DB_SAVE,
		DB_LOAD,
		DB_DELETE
	};

	DisplayState display = DS_NONE;

	SavedataParam param;
	SceUtilitySavedataParam request{};
	// For detecting changes made by the game.
	SceUtilitySavedataParam originalRequest{};
	u32 requestAddr = 0;
	int currentSelectedSave = 0;

	enum SaveIOStatus {
		SAVEIO_NONE,
		SAVEIO_PENDING,
		SAVEIO_DONE,
	};

	std::thread *ioThread = nullptr;
	std::mutex paramLock;
	volatile SaveIOStatus ioThreadStatus;
};

