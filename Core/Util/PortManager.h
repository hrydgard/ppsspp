// Copyright (c) 2013- PPSSPP Project.

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

// Most of the code are based on https://github.com/RJ/libportfwd and updated to the latest miniupnp library
// All credit goes to him and the official miniupnp project! http://miniupnp.free.fr/


#pragma once

#ifdef USE_SYSTEM_MINIUPNPC
#include <miniupnpc/include/miniwget.h>
#include <miniupnpc/include/miniupnpc.h>
#include <miniupnpc/include/upnpcommands.h>
#else
#ifndef MINIUPNP_STATICLIB
#define MINIUPNP_STATICLIB
#endif
#include "ext/miniupnp/miniupnpc/include/miniwget.h"
#include "ext/miniupnp/miniupnpc/include/miniupnpc.h"
#include "ext/miniupnp/miniupnpc/include/upnpcommands.h"
#endif

#include <string>
#include <deque>

struct UPnPArgs {
	int cmd;
	std::string protocol;
	unsigned short port;
	unsigned short intport;
};

#define IP_PROTOCOL_TCP	"TCP"
#define IP_PROTOCOL_UDP	"UDP"
#define UPNP_INITSTATE_NONE	0
#define UPNP_INITSTATE_BUSY	1
#define UPNP_INITSTATE_DONE	2

#define UPNP_CMD_ADD	0
#define UPNP_CMD_REMOVE	1

struct UPNPUrls;
struct IGDdatas;

struct PortMap {
	bool taken;
	std::string protocol;
	std::string extPort_str;
	std::string intPort_str;
	std::string lanip;
	std::string remoteHost;
	std::string desc;
	std::string duration;
	std::string enabled;
};

class PortManager {
public:
	PortManager();
	~PortManager();

	// Initialize UPnP
	// timeout: milliseconds to wait for a router to respond (default = 2000 ms)
	bool Initialize(const unsigned int timeout = 2000);

	// Get UPnP Initialization status
	int GetInitState();

	// Add a port & protocol (TCP, UDP or vendor-defined) to map for forwarding (intport = 0 : same as [external] port)
	bool Add(const char* protocol, unsigned short port, unsigned short intport = 0);

	// Remove a port mapping (external port)
	bool Remove(const char* protocol, unsigned short port);

	// Call on exit. Does a full shutdown.
	void Shutdown();

private:
	// Retrieves port lists mapped by PPSSPP for current LAN IP & other's applications
	bool RefreshPortList();

	// Removes any lingering mapped ports created by PPSSPP (including from previous crashes)
	bool Clear();

	// Restore ports mapped by others that were taken by PPSSPP, better used after Clear()
	bool Restore();

	// Uninitialize/Reset the state
	void Terminate();

	struct UPNPUrls* urls = nullptr;
	struct IGDdatas* datas = nullptr;

	int m_InitState = UPNP_INITSTATE_NONE;
	int m_LocalPort = UPNP_LOCAL_PORT_ANY;
	std::string m_lanip;
	std::string m_defaultDesc;
	std::string m_leaseDuration = "43200"; // range(0-604800) in seconds (0 = Indefinite/permanent). Some routers doesn't support non-zero value
	std::deque<std::pair<std::string, std::string>> m_portList;
	std::deque<PortMap> m_otherPortList;
};

extern PortManager g_PortManager;

void __UPnPInit(const unsigned int timeout = 2000);
void __UPnPShutdown();

// Add a port & protocol (TCP, UDP or vendor-defined) to map for forwarding (intport = 0 : same as [external] port)
void UPnP_Add(const char* protocol, unsigned short port, unsigned short intport = 0);

// Remove a port mapping (external port)
void UPnP_Remove(const char* protocol, unsigned short port);
