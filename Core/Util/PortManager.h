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
#include <miniupnpc/miniwget.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
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

#define IP_PROTOCOL_TCP	"TCP"
#define IP_PROTOCOL_UDP	"UDP"

enum {
	UPNP_INITSTATE_NONE	= 0,
	UPNP_INITSTATE_BUSY	= 1,
	UPNP_INITSTATE_DONE	= 2,
};

enum {
	UPNP_CMD_ADD = 0,
	UPNP_CMD_REMOVE = 1,
};

struct UPnPArgs {
	int cmd = UPNP_CMD_ADD;
	std::string protocol;
	unsigned short port = 0;
	unsigned short intport = 0;
	// Description to register the mapping under. Built when the request is queued, on the thread
	// that owns the game state, since the UPnP service thread can't safely read it later.
	std::string desc;
	// How many times we've failed to reach the router about this request. Bounded so a request
	// can't get retried forever, blocking everything queued behind it.
	int attempts = 0;
};

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

// Only ever touched by the UPnP service thread (see PortManager.cpp). Don't call into it
// from anywhere else - queue a request with UPnP_Add()/UPnP_Remove() instead.
class PortManager {
public:
	// Discover a router and pick up any mappings we left behind earlier.
	// timeout: milliseconds to wait for a router to respond.
	bool Initialize(unsigned int timeout = 2000);

	int GetInitState() const { return m_InitState; }

	// Add a port & protocol (TCP, UDP or vendor-defined) to map for forwarding (intport = 0 : same as [external] port)
	bool Add(const char *protocol, unsigned short port, unsigned short intport, const std::string &desc);

	// Remove a port mapping (external port)
	bool Remove(const char *protocol, unsigned short port);

	// Drops our mappings, restores any that we took over, and resets to the uninitialized state.
	// budgetSeconds bounds how long we're willing to keep talking to the router: a router that has
	// gone away answers with socket timeouts, which would otherwise stall app exit for a long time.
	void Shutdown(double budgetSeconds = 3.0);

private:
	// Retrieves port lists mapped by PPSSPP for current LAN IP & other's applications
	bool RefreshPortList();

	// Removes the port mappings we know PPSSPP created (including leftovers from previous crashes,
	// which RefreshPortList() picks up at init time).
	bool Clear();

	// Restore ports mapped by others that were taken by PPSSPP, better used after Clear()
	bool Restore();

	// Uninitialize/Reset the state
	void Terminate();

	bool HaveControlURL() const;
	// True once the current operation has used up its time budget, see Shutdown().
	bool OutOfTime() const;

	UPNPUrls m_urls{};
	IGDdatas m_datas{};
	bool m_urlsValid = false;

	int m_InitState = UPNP_INITSTATE_NONE;
	int m_LocalPort = UPNP_LOCAL_PORT_ANY;
	double m_deadline = 0.0;
	std::string m_lanip;
	std::string m_leaseDuration;
	std::deque<std::pair<std::string, std::string>> m_portList;
	std::deque<PortMap> m_otherPortList;
};

extern PortManager g_PortManager;

void __UPnPInit(unsigned int timeout_ms);
void __UPnPShutdown();

// Add a port & protocol (TCP, UDP or vendor-defined) to map for forwarding (intport = 0 : same as [external] port)
void UPnP_Add(const char *protocol, unsigned short port, unsigned short intport = 0);

// Remove a port mapping (external port)
void UPnP_Remove(const char *protocol, unsigned short port);

// Wakes the UPnP service thread immediately, without queuing a request - call this after
// changing the enable setting so it can connect (or tear down its mappings) right away
// instead of waiting for the next retry.
void UPnP_Notify();
