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

#define SCE_UTILITY_SAVEDATA_ERROR_TYPE                 (0x80110300)

#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_MS           (0x80110301)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_EJECT_MS        (0x80110302)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_ACCESS_ERROR    (0x80110305)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN     (0x80110306)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA         (0x80110307)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM           (0x80110308)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_FILE_NOT_FOUND  (0x80110309)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_INTERNAL        (0x8011030b)

#define SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK       (0x80110321)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_MEMSTICK_FULL     (0x80110323)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN       (0x80110326)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA           (0x80110327)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS        (0x80110328)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND    (0x80110329)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_STATUS        (0x8011032c)

#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_MS           (0x80110381)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_EJECT_MS        (0x80110382)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE      (0x80110383)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_PROTECTED    (0x80110384)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR    (0x80110385)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM           (0x80110388)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_UMD          (0x80110389)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_WRONG_UMD       (0x8011038a)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_INTERNAL        (0x8011038b)

#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_MS         (0x80110341)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_EJECT_MS      (0x80110342)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_MS_PROTECTED  (0x80110344)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_ACCESS_ERROR  (0x80110345)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA       (0x80110347)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_PARAM         (0x80110348)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_INTERNAL      (0x8011034b)

#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_MS          (0x801103C1)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_EJECT_MS       (0x801103C2)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_ACCESS_ERROR   (0x801103C5)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA        (0x801103C7)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_PARAM          (0x801103C8)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_UMD         (0x801103C9)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_WRONG_UMD      (0x801103Ca)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_INTERNAL       (0x801103Cb)

class PSPSaveDialog: public PSPDialog {
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

	enum DisplayState
	{
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

	enum DialogBanner
	{
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

	int yesnoChoice = 0;

	enum SaveIOStatus
	{
		SAVEIO_NONE,
		SAVEIO_PENDING,
		SAVEIO_DONE,
	};

	std::thread *ioThread = nullptr;
	std::mutex paramLock;
	volatile SaveIOStatus ioThreadStatus;
};

