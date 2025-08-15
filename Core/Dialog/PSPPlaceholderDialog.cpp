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

#include "PSPPlaceholderDialog.h"

PSPPlaceholderDialog::PSPPlaceholderDialog(UtilityDialogType type) : PSPDialog(type) {

}

PSPPlaceholderDialog::~PSPPlaceholderDialog() {
}


int PSPPlaceholderDialog::Init() {
	ChangeStatus(SCE_UTILITY_STATUS_INITIALIZE, 0);
	InitCommon();
	return 0;
}

int PSPPlaceholderDialog::Update(int animSpeed) {
	if (ReadStatus() == SCE_UTILITY_STATUS_INITIALIZE) {
		ChangeStatus(SCE_UTILITY_STATUS_RUNNING, 0);
	} else if (ReadStatus() == SCE_UTILITY_STATUS_RUNNING) {
		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
	} else if (ReadStatus() == SCE_UTILITY_STATUS_FINISHED) {
		ChangeStatus(SCE_UTILITY_STATUS_SHUTDOWN, 0);
	}
	UpdateCommon();

	return 0;
}
