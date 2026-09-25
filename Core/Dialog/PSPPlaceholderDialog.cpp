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
#include "Core/Dialog/PSPPlaceholderDialog.h"
#include "Core/HLE/ErrorCodes.h"

// Rough guesses, like the other dialogs'.
static const int PLACEHOLDER_INIT_DELAY_US = 200000;
static const int PLACEHOLDER_SHUTDOWN_DELAY_US = 2000;

PSPPlaceholderDialog::PSPPlaceholderDialog(UtilityDialogType type) : PSPDialog(type) {
}

int PSPPlaceholderDialog::Init(u32 paramAddr) {
	if (ReadStatus() != SCE_UTILITY_STATUS_NONE) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}
	// The request sizes a PSP accepts for GameSharing.
	const int check = DialogType() == UtilityDialogType::GAMESHARING ? CheckRequest(paramAddr, { 0x50, 0x54, 0x64 }) : 0;
	if (check < 0) {
		return check;
	}
	if (!Memory::IsValidRange(paramAddr, sizeof(pspUtilityDialogCommon))) {
		return SCE_KERNEL_ERROR_BAD_ARGUMENT;
	}
	params_ = paramAddr;

	ChangeStatusInit(PLACEHOLDER_INIT_DELAY_US);
	InitCommon();
	return 0;
}

int PSPPlaceholderDialog::Update(int animSpeed) {
	if (ReadStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	if (params_.IsValid()) {
		params_->result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
		params_.NotifyWrite("DialogResult");
	}
	ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
	return 0;
}

int PSPPlaceholderDialog::Shutdown(bool force) {
	if (ReadStatus() != SCE_UTILITY_STATUS_FINISHED && !force) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(PLACEHOLDER_SHUTDOWN_DELAY_US);
	}
	return 0;
}

void PSPPlaceholderDialog::DoState(PointerWrap &p) {
	PSPDialog::DoState(p);

	auto s = p.Section("PSPPlaceholderDialog", 1, 1);
	if (!s)
		return;

	Do(p, params_);
}

pspUtilityDialogCommon *PSPPlaceholderDialog::GetCommonParam() {
	if (params_.IsValid())
		return params_;
	return nullptr;
}
