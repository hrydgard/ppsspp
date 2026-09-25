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

#include <string>

#include "Core/Dialog/PSPDialog.h"
#include "Core/MemMap.h"

// Stands in for the PSP's web browser: shows the URL the game wants to open and offers to open it in
// the host's browser instead.
//
// On a PSP the HtmlViewer has its own state, apart from the other dialogs: it doesn't wait for them,
// and they don't wait for it (utility/dialog/htmlviewer).
class PSPHtmlViewerDialog : public PSPDialog {
public:
	PSPHtmlViewerDialog(UtilityDialogType type);

	int Init(u32 paramAddr);
	int Update(int animSpeed) override;
	int Shutdown(bool force = false) override;
	void DoState(PointerWrap &p) override;
	void ResetState() override;
	pspUtilityDialogCommon *GetCommonParam() override;

	// Until one has started, all the HtmlViewer calls return WRONG_TYPE.
	bool HasStarted() const {
		return started_;
	}

protected:
	bool UseAutoStatus() override {
		return false;
	}
	bool LocksVolatileMemory() const override {
		return false;
	}

private:
	void FreeWorkMemory();

	PSPPointer<pspUtilityDialogCommon> params_;
	std::string url_;
	u32 workMemory_ = 0;
	bool started_ = false;
};
