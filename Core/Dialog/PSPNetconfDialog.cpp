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

#include "PSPNetconfDialog.h"
#include "ChunkFile.h"
#include "../Core/MemMap.h"
#include "../Core/HLE/sceNetAdhoc.h"

#define NETCONF_CONNECT_ADHOC 2
#define NETCONF_CREATE_ADHOC 4
#define NETCONF_JOIN_ADHOC 5

PSPNetconfDialog::PSPNetconfDialog() {
}

PSPNetconfDialog::~PSPNetconfDialog() {
}

int PSPNetconfDialog::Init(u32 paramAddr) {
	// Already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	status = SCE_UTILITY_STATUS_INITIALIZE;
	return 0;
}

int PSPNetconfDialog::Update(int animSpeed) {
  if(request.netAction == NETCONF_CONNECT_ADHOC ||
      request.netAction == NETCONF_CREATE_ADHOC ||
      request.netAction == NETCONF_JOIN_ADHOC){
    if(request.NetconfData != NULL){
      Shutdown(true);
        if(sceNetAdhocctlCreate(request.NetconfData->groupName) == 0)
        {
            status = SCE_UTILITY_STATUS_FINISHED;
            return 0;
        }
        return -1;
    }
  }
	return 0;
}

int PSPNetconfDialog::Shutdown(bool force) {
	if (status != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	return PSPDialog::Shutdown();
}

void PSPNetconfDialog::DoState(PointerWrap &p) {	
	PSPDialog::DoState(p);

	auto s = p.Section("PSPNetconfigDialog", 0, 1);
	if (!s)
		return;

	p.Do(request);
}
