// Copyright (c) 2021- PPSSPP Project.

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

#include "Windows/W32Util/ContextMenu.h"
#include "Windows/resource.h"

static HMENU g_hPopupMenus;

void ContextMenuInit(HINSTANCE inst) {
	g_hPopupMenus = LoadMenu(inst, (LPCWSTR)IDR_POPUPMENUS);
}

ContextPoint ContextPoint::FromCursor() {
	ContextPoint result;
	GetCursorPos(&result.pos_);
	return result;
}

ContextPoint ContextPoint::FromClient(const POINT &clientPoint) {
	ContextPoint result;
	result.pos_ = clientPoint;
	result.isClient_ = true;
	return result;
}

ContextPoint ContextPoint::FromEvent(LPARAM lParam) {
	ContextPoint result;
	result.pos_.x = LOWORD(lParam);
	result.pos_.y = HIWORD(lParam);
	result.isClient_ = true;
	return result;
}

HMENU GetContextMenu(ContextMenuID which) {
	return GetSubMenu(g_hPopupMenus, (int)which);
}

int TriggerContextMenu(ContextMenuID which, HWND wnd, const ContextPoint &pt) {
	POINT pos = pt.pos_;
	if (pt.isClient_) {
		ClientToScreen(wnd, &pos);
	}

	HMENU menu = GetContextMenu(which);
	if (!menu)
		return -1;

	return TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pos.x, pos.y, wnd, 0);
}
