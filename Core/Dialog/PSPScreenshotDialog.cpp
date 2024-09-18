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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Dialog/PSPDialog.h"
#include "Core/Dialog/PSPScreenshotDialog.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

enum SceUtilityScreenshotType {
	SCE_UTILITY_SCREENSHOT_TYPE_GUI = 0,
	SCE_UTILITY_SCREENSHOT_TYPE_AUTO = 1,
	SCE_UTILITY_SCREENSHOT_TYPE_SAVE = 2,
	SCE_UTILITY_SCREENSHOT_TYPE_VIEW = 3,

	// Names are just guesses based on status value behavior.
	SCE_UTILITY_SCREENSHOT_TYPE_CONT_START = 100,
	SCE_UTILITY_SCREENSHOT_TYPE_CONT_FINISH = 101,
	SCE_UTILITY_SCREENSHOT_TYPE_CONT_STOP = 102,
};

static const int SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V1 = 436;
static const int SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V2 = 928;
static const int SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V3 = 932;

#if COMMON_LITTLE_ENDIAN
typedef SceUtilityScreenshotType SceUtilityScreenshotType_le;
#else
typedef swap_struct_t<SceUtilityScreenshotType, swap_32_t<SceUtilityScreenshotType> > SceUtilityScreenshotType_le;
#endif

struct SceUtilityScreenshotParams {
	pspUtilityDialogCommon base;
	SceUtilityScreenshotType_le mode;

	// TODO
};

PSPScreenshotDialog::PSPScreenshotDialog(UtilityDialogType type) : PSPDialog(type) {
}

PSPScreenshotDialog::~PSPScreenshotDialog() {
}

int PSPScreenshotDialog::Init(u32 paramAddr) {
	// Already running
	if (ReadStatus() != SCE_UTILITY_STATUS_NONE && ReadStatus() != SCE_UTILITY_STATUS_SHUTDOWN) {
		ERROR_LOG_REPORT(Log::HLE, "sceUtilityScreenshotInitStart(%08x): invalid status", paramAddr);
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	params_ = PSPPointer<SceUtilityScreenshotParams>::Create(paramAddr);
	if (!params_.IsValid()) {
		ERROR_LOG_REPORT(Log::HLE, "sceUtilityScreenshotInitStart(%08x): invalid pointer", paramAddr);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	switch ((u32)params_->base.size) {
	case SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V1:
	case SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V2:
	case SCE_UTILITY_SCREENSHOTDIALOG_SIZE_V3:
		break;

	default:
		ERROR_LOG_REPORT(Log::HLE, "sceUtilityScreenshotInitStart(%08x): invalid size %d", paramAddr, (u32)params_->base.size);
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}

	mode = params_->mode;
	ChangeStatus(SCE_UTILITY_STATUS_INITIALIZE, 0);
	InitCommon();

	return 0;
}

int PSPScreenshotDialog::Update(int animSpeed) {
	UpdateCommon();
	if (UseAutoStatus()) {
		if (ReadStatus() == SCE_UTILITY_STATUS_INITIALIZE) {
			ChangeStatus(SCE_UTILITY_STATUS_RUNNING, 0);
		} else if (ReadStatus() == SCE_UTILITY_STATUS_RUNNING) {
			if (mode == SCE_UTILITY_SCREENSHOT_TYPE_CONT_START) {
				ChangeStatus(SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN, 0);
			} else {
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
			}
		} else if (ReadStatus() == SCE_UTILITY_STATUS_FINISHED) {
			ChangeStatus(SCE_UTILITY_STATUS_SHUTDOWN, 0);
		}
	}
	return 0;
}

int PSPScreenshotDialog::ContStart() {
	// Based on JPCSP http://code.google.com/p/jpcsp/source/detail?r=3381
	if (ReadStatus() != SCE_UTILITY_STATUS_SCREENSHOT_UNKNOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	// Check with JPCSPTrace log of Dream Club Portable
	ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);

	return 0;
}

void PSPScreenshotDialog::DoState(PointerWrap &p) {
	PSPDialog::DoState(p);

	auto s = p.Section("PSPScreenshotDialog", 0, 2);
	if (!s)
		return;

	Do(p, mode);
	if (s >= 2) {
		Do(p, params_);
	}
}

pspUtilityDialogCommon *PSPScreenshotDialog::GetCommonParam() {
	if (params_.IsValid())
		return &params_->base;
	return nullptr;
}
