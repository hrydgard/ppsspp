// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

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

// Most of the code are based on https://github.com/RJ/libportfwd and updated to the latest miniupnp library
// All credit goes to him and the official miniupnp project! http://miniupnp.free.fr/


#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Net/Resolve.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/OSD.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/PortManager.h"

PortManager g_PortManager;
bool upnpServiceRunning = false;
std::thread upnpServiceThread;
std::recursive_mutex upnpLock;
std::deque<UPnPArgs> upnpReqs;

PortManager::PortManager(): 
	m_InitState(UPNP_INITSTATE_NONE),
	m_LocalPort(UPNP_LOCAL_PORT_ANY),
	m_leaseDuration("43200") {
	// Don't call net::Init or similar here, we don't want stuff like that to happen before main.
}

PortManager::~PortManager() {
	// FIXME: On Windows it seems using any UPnP functions in this destructor that gets triggered when exiting PPSSPP will resulting to UPNPCOMMAND_HTTP_ERROR due to early WSACleanup (miniupnpc was getting WSANOTINITIALISED internally)
}

void PortManager::Shutdown() {
	Clear();
	Restore();
	Terminate();
}

void PortManager::Terminate() {
	VERBOSE_LOG(Log::sceNet, "PortManager::Terminate()");
	if (urls) {
#ifdef WITH_UPNP
		FreeUPNPUrls(urls);
#endif
		free(urls);
		urls = NULL;
	}
	if (datas) {
		free(datas);
		datas = NULL;
	}
	m_otherPortList.clear(); m_otherPortList.shrink_to_fit();
	m_portList.clear(); m_portList.shrink_to_fit();
	m_lanip.clear();
	m_defaultDesc.clear();
	m_leaseDuration.clear();
	m_LocalPort = UPNP_LOCAL_PORT_ANY;
	m_InitState = UPNP_INITSTATE_NONE;
}

bool PortManager::Initialize(const unsigned int timeout) {
#ifdef WITH_UPNP
	// Windows: Assuming WSAStartup already called beforehand
	struct UPNPDev* devlist;
	struct UPNPDev* dev;
	char* descXML;
	int descXMLsize = 0;
	int descXMLstatus = 0;
	int localport = m_LocalPort; // UPNP_LOCAL_PORT_ANY (0), or UPNP_LOCAL_PORT_SAME (1) as an alias for 1900 (for backwards compatability?)
	int ipv6 = 0; // 0 = IPv4, 1 = IPv6
	unsigned char ttl = 2; // defaulting to 2
	int error = 0;
	
	VERBOSE_LOG(Log::sceNet, "PortManager::Initialize(%d)", timeout);
	if (!g_Config.bEnableUPnP) {
		ERROR_LOG(Log::sceNet, "PortManager::Initialize - UPnP is Disabled on Networking Settings");
		return false;
	}

	if (m_InitState != UPNP_INITSTATE_NONE) {
		switch (m_InitState)
		{
		case UPNP_INITSTATE_BUSY: {
			WARN_LOG(Log::sceNet, "PortManager - Initialization already in progress");
			return false;
		}
		// Should we redetect UPnP? just in case the player switched to a different network in the middle
		case UPNP_INITSTATE_DONE: {
			WARN_LOG(Log::sceNet, "PortManager - Already Initialized");
			return true;
		}
		default:
			break;
		}
	}

	m_leaseDuration = "43200"; // 12 hours
	m_InitState = UPNP_INITSTATE_BUSY;
	urls = (UPNPUrls*)malloc(sizeof(struct UPNPUrls));
	if (!urls)
		return false;
	datas = (IGDdatas*)malloc(sizeof(struct IGDdatas));
	if (!datas) {
		free(urls);
		return false;
	}
	memset(urls, 0, sizeof(struct UPNPUrls));
	memset(datas, 0, sizeof(struct IGDdatas));

	devlist = upnpDiscover(timeout, NULL, NULL, localport, ipv6, ttl, &error);
	if (devlist)
	{
		dev = devlist;
		while (dev)
		{
			if (strstr(dev->st, "InternetGatewayDevice"))
				break;
			dev = dev->pNext;
		}
		if (!dev)
			dev = devlist; // defaulting to first device

		INFO_LOG(Log::sceNet, "PortManager - UPnP device: [desc: %s] [st: %s]", dev->descURL, dev->st);

		descXML = (char*)miniwget(dev->descURL, &descXMLsize, dev->scope_id, &descXMLstatus);
		if (descXML)
		{
			parserootdesc(descXML, descXMLsize, datas);
			free(descXML); descXML = 0;
			GetUPNPUrls(urls, datas, dev->descURL, dev->scope_id);
		}

		// Get LAN IP address that connects to the router
		char lanaddr[64] = "unset";

		// possible "status" values:
		// -1 = Internal error
		//  0 = NO IGD found
		//  1 = A valid connected IGD has been found
		//  2 = A valid connected IGD has been found but its IP address is reserved (non routable)
		//  3 = A valid IGD has been found but it reported as not connected
		//  4 = an UPnP device has been found but was not recognized as an IGD
#if (MINIUPNPC_API_VERSION >= 18)
		int status = UPNP_GetValidIGD(devlist, urls, datas, lanaddr, sizeof(lanaddr), nullptr, 0);
#else
		int status = UPNP_GetValidIGD(devlist, urls, datas, lanaddr, sizeof(lanaddr));
#endif
		m_lanip = std::string(lanaddr);
		INFO_LOG(Log::sceNet, "PortManager - Detected LAN IP: %s (status=%d)", m_lanip.c_str(), status);

		// Additional Info
		char connectionType[64] = "";
		if (UPNP_GetConnectionTypeInfo(urls->controlURL, datas->first.servicetype, connectionType) != UPNPCOMMAND_SUCCESS) {
			WARN_LOG(Log::sceNet, "PortManager - GetConnectionTypeInfo failed");
		}
		else {
			INFO_LOG(Log::sceNet, "PortManager - Connection Type: %s", connectionType);
		}

		// Using Game ID & Player Name as default description for mapping
		std::string gameID = g_paramSFO.GetDiscID();
		m_defaultDesc = "PPSSPP:" + gameID + ":" + g_Config.sNickName; // Some routers may automatically prefixed it with "UPnP:"

		freeUPNPDevlist(devlist);

		//m_LocalPort = localport; // We shouldn't keep the right port for the next game reset if we wanted to redetect UPnP
		m_InitState = UPNP_INITSTATE_DONE;
		RefreshPortList();
		return true;
	}

	ERROR_LOG(Log::sceNet, "PortManager - upnpDiscover failed (error: %i) or No UPnP device detected", error);
	if (g_Config.bEnableUPnP) {
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		g_OSD.Show(OSDType::MESSAGE_ERROR, n->T("Unable to find UPnP device"));
	}
	m_InitState = UPNP_INITSTATE_NONE;
#endif // WITH_UPNP
	return false;
}

int PortManager::GetInitState() {
	return m_InitState;
}

bool PortManager::Add(const char* protocol, unsigned short port, unsigned short intport) {
#ifdef WITH_UPNP
	char port_str[16];
	char intport_str[16];
	int r;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	
	if (intport == 0)
		intport = port;
	INFO_LOG(Log::sceNet, "PortManager::Add(%s, %d, %d)", protocol, port, intport);
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) {
			WARN_LOG(Log::sceNet, "PortManager::Add - the init was not done !");
			g_OSD.Show(OSDType::MESSAGE_INFO, n->T("UPnP need to be reinitialized"));
		}
		Terminate();
		return false;
	}

	snprintf(port_str, sizeof(port_str), "%d", port);
	snprintf(intport_str, sizeof(intport_str), "%d", intport);
	// Only add new port map if it's not previously created by PPSSPP for current IP
	auto el_it = std::find_if(m_portList.begin(), m_portList.end(),
		[port_str, protocol](const std::pair<std::string, std::string> &el) { return el.first == port_str && el.second == protocol; });
	if (el_it == m_portList.end()) {
		auto el_it = std::find_if(m_otherPortList.begin(), m_otherPortList.end(),
			[port_str, protocol](const PortMap& el) { return el.extPort_str == port_str && el.protocol == protocol; });
		if (el_it != m_otherPortList.end()) {
			// Try to delete the port mapping before we create it, just in case we have dangling port mapping from the daemon not being shut down correctly or the port was taken by other
			r = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, port_str, protocol, NULL);
		}
		r = UPNP_AddPortMapping(urls->controlURL, datas->first.servicetype,
			port_str, intport_str, m_lanip.c_str(), m_defaultDesc.c_str(), protocol, NULL, m_leaseDuration.c_str());
		if (r == 725 && m_leaseDuration != "0") {
			m_leaseDuration = "0";
			r = UPNP_AddPortMapping(urls->controlURL, datas->first.servicetype,
				port_str, intport_str, m_lanip.c_str(), m_defaultDesc.c_str(), protocol, NULL, m_leaseDuration.c_str());
		}
		if (r != 0)
		{
			ERROR_LOG(Log::sceNet, "PortManager - AddPortMapping failed (error: %i)", r);
			if (r == UPNPCOMMAND_HTTP_ERROR) {
				if (g_Config.bEnableUPnP) {
					g_OSD.Show(OSDType::MESSAGE_INFO, n->T("UPnP need to be reinitialized"));
				}
				Terminate(); // Most of the time errors occurred because the router is no longer reachable (ie. changed networks) so we should invalidate the state to prevent further lags due to timeouts
				return false;
			}
		}
		m_portList.push_front({ port_str, protocol });
		// Keep tracks of it to be restored later if it belongs to others
		if (el_it != m_otherPortList.end()) el_it->taken = true;
	}
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Remove(const char* protocol, unsigned short port) {
#ifdef WITH_UPNP
	char port_str[16];
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	INFO_LOG(Log::sceNet, "PortManager::Remove(%s, %d)", protocol, port);
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) {
			WARN_LOG(Log::sceNet, "PortManager::Remove - the init was not done !");
			g_OSD.Show(OSDType::MESSAGE_INFO, n->T("UPnP need to be reinitialized"));
		}
		Terminate();
		return false;
	}
	snprintf(port_str, sizeof(port_str), "%d", port);
	int r = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, port_str, protocol, NULL);
	if (r != 0)
	{
		ERROR_LOG(Log::sceNet, "PortManager - DeletePortMapping failed (error: %i)", r);
		if (r == UPNPCOMMAND_HTTP_ERROR) {
			if (g_Config.bEnableUPnP) {
				g_OSD.Show(OSDType::MESSAGE_INFO, n->T("UPnP need to be reinitialized"));
			}
			Terminate(); // Most of the time errors occurred because the router is no longer reachable (ie. changed networks) so we should invalidate the state to prevent further lags due to timeouts
			return false;
		}
	}
	for (auto it = m_portList.begin(); it != m_portList.end(); ) {
		(it->first == port_str && it->second == protocol) ? it = m_portList.erase(it) : ++it;
	}
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Restore() {
#ifdef WITH_UPNP
	int r;
	VERBOSE_LOG(Log::sceNet, "PortManager::Restore()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(Log::sceNet, "PortManager::Remove - the init was not done !");
		return false;
	}
	for (auto it = m_otherPortList.begin(); it != m_otherPortList.end(); ++it) {
		if (it->taken) {
			auto port_str = it->extPort_str;
			auto protocol = it->protocol;
			// Remove it first if it's still being taken by PPSSPP
			auto el_it = std::find_if(m_portList.begin(), m_portList.end(),
				[port_str, protocol](const std::pair<std::string, std::string>& el) { return el.first == port_str && el.second == protocol; });
			if (el_it != m_portList.end()) {
				r = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, port_str.c_str(), protocol.c_str(), NULL);
				if (r == 0) {
					m_portList.erase(el_it);
				}
				else {
					ERROR_LOG(Log::sceNet, "PortManager::Restore - DeletePortMapping failed (error: %i)", r);
					if (r == UPNPCOMMAND_HTTP_ERROR)
						return false; // Might be better not to exit here, but exiting a loop will avoid long timeouts in the case the router is no longer reachable
				}
			}
			// Add the original owner back
			r = UPNP_AddPortMapping(urls->controlURL, datas->first.servicetype, 
				it->extPort_str.c_str(), it->intPort_str.c_str(), it->lanip.c_str(), it->desc.c_str(), it->protocol.c_str(), it->remoteHost.c_str(), it->duration.c_str());
			if (r == 0) {
				it->taken = false;
			}
			else {
				ERROR_LOG(Log::sceNet, "PortManager::Restore - AddPortMapping failed (error: %i)", r);
				if (r == UPNPCOMMAND_HTTP_ERROR)
					return false; // Might be better not to exit here, but exiting a loop will avoid long timeouts in the case the router is no longer reachable
			}		
		}
	}
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Clear() {
#ifdef WITH_UPNP
	int r;
	int i = 0;
	char index[16];
	char intAddr[40];
	char intPort[6];
	char extPort[6];
	char protocol[4];
	char desc[80];
	char enabled[6];
	char rHost[64];
	char duration[16];

	VERBOSE_LOG(Log::sceNet, "PortManager::Clear()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(Log::sceNet, "PortManager::Clear - the init was not done !");
		return false;
	}
	//unsigned int num = 0;
	//UPNP_GetPortMappingNumberOfEntries(urls->controlURL, datas->first.servicetype, &num); // Not supported by many routers
	do {
		snprintf(index, sizeof(index), "%d", i);
		rHost[0] = '\0'; enabled[0] = '\0';
		duration[0] = '\0'; desc[0] = '\0'; protocol[0] = '\0';
		extPort[0] = '\0'; intPort[0] = '\0'; intAddr[0] = '\0';
		// May gets UPNPCOMMAND_HTTP_ERROR when called while exiting PPSSPP (ie. used in destructor)
		r = UPNP_GetGenericPortMappingEntry(urls->controlURL,
			datas->first.servicetype,
			index,
			extPort, intAddr, intPort,
			protocol, desc, enabled,
			rHost, duration);
		// Only removes port mappings created by PPSSPP for current LAN IP
		if (r == 0 && intAddr == m_lanip && std::string(desc).find("PPSSPP:") != std::string::npos) {
			int r2 = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, extPort, protocol, rHost);
			if (r2 != 0)
			{
				ERROR_LOG(Log::sceNet, "PortManager::Clear - DeletePortMapping(%s, %s) failed (error: %i)", extPort, protocol, r2);
				if (r2 == UPNPCOMMAND_HTTP_ERROR)
					return false;
			}
			else {
				i--;
				for (auto it = m_portList.begin(); it != m_portList.end(); ) {
					(it->first == extPort && it->second == protocol) ? it = m_portList.erase(it) : ++it;
				}
			}
		}
		i++;
	} while (r == 0 && i < 65536);
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::RefreshPortList() {
#ifdef WITH_UPNP
	int r;
	int i = 0;
	char index[16];
	char intAddr[40];
	char intPort[6];
	char extPort[6];
	char protocol[4];
	char desc[80];
	char enabled[6];
	char rHost[64];
	char duration[16];

	INFO_LOG(Log::sceNet, "PortManager::RefreshPortList()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(Log::sceNet, "PortManager::RefreshPortList - the init was not done !");
		return false;
	}
	m_portList.clear();
	m_otherPortList.clear();
	//unsigned int num = 0;
	//UPNP_GetPortMappingNumberOfEntries(urls->controlURL, datas->first.servicetype, &num); // Not supported by many routers
	do {
		snprintf(index, sizeof(index), "%d", i);
		rHost[0] = '\0'; enabled[0] = '\0';
		duration[0] = '\0'; desc[0] = '\0'; protocol[0] = '\0';
		extPort[0] = '\0'; intPort[0] = '\0'; intAddr[0] = '\0';
		r = UPNP_GetGenericPortMappingEntry(urls->controlURL,
			datas->first.servicetype,
			index,
			extPort, intAddr, intPort,
			protocol, desc, enabled,
			rHost, duration);
		if (r == 0) {
			std::string desc_str = std::string(desc);
			// Some router might prefix the description with "UPnP:" so we may need to truncate it to prevent it from getting multiple prefix when restored later
			if (desc_str.find("UPnP:") == 0)
				desc_str = desc_str.substr(5);
			// Only include port mappings created by PPSSPP for current LAN IP
			if (intAddr == m_lanip && desc_str.find("PPSSPP:") != std::string::npos) {
				m_portList.push_back({ extPort, protocol });
			}
			// Port mappings belong to others that might be taken by PPSSPP later
			else {
				m_otherPortList.push_back({ false, protocol, extPort, intPort, intAddr, rHost, desc_str, duration, enabled });
			}
		}
		i++;
	} while (r == 0 && i < 65536);
	return true;
#else
	return false;
#endif // WITH_UPNP
}

int upnpService(const unsigned int timeout)
{
	SetCurrentThreadName("UPnPService");
	INFO_LOG(Log::sceNet, "UPnPService: Begin of UPnPService Thread");

	// Service Loop
	while (upnpServiceRunning && coreState != CORE_POWERDOWN) {
		// Attempts to reconnect if not connected yet or got disconnected
		if (g_Config.bEnableUPnP && g_PortManager.GetInitState() == UPNP_INITSTATE_NONE) {
			g_PortManager.Initialize(timeout);
		}

		if (g_Config.bEnableUPnP && g_PortManager.GetInitState() == UPNP_INITSTATE_DONE && !upnpReqs.empty()) {
			upnpLock.lock();
			UPnPArgs arg = upnpReqs.front();
			upnpLock.unlock();

			bool ok = true;
			switch (arg.cmd) {
				case UPNP_CMD_ADD:
					ok = g_PortManager.Add(arg.protocol.c_str(), arg.port, arg.intport);
					break;
				case UPNP_CMD_REMOVE:
					ok = g_PortManager.Remove(arg.protocol.c_str(), arg.port);
					break;
				default:
					break;
			}

            // It's only considered failed when disconnected (should be retried when reconnected)
			if (ok) {
                upnpLock.lock();
                upnpReqs.pop_front();
                upnpLock.unlock();
            }
		}

		// Sleep for 1ms for faster response
		sleep_ms(1);
	}

	// Cleaning up regardless of g_Config.bEnableUPnP to prevent lingering open ports on the router
	if (g_PortManager.GetInitState() == UPNP_INITSTATE_DONE) {
		g_PortManager.Shutdown();
	}

	// Should we ingore any leftover UPnP requests? instead of processing it on the next game start
	upnpLock.lock();
	upnpReqs.clear();
	upnpLock.unlock();

	INFO_LOG(Log::sceNet, "UPnPService: End of UPnPService Thread");
	return 0;
}

void __UPnPInit(const unsigned int timeout) {
	if (!upnpServiceRunning) {
		upnpServiceRunning = true;
		upnpServiceThread = std::thread(upnpService, timeout);
	}
}

void __UPnPShutdown() {
	if (upnpServiceRunning) {
		upnpServiceRunning = false;
		if (upnpServiceThread.joinable()) {
			upnpServiceThread.join();
		}
	}
}

void UPnP_Add(const char* protocol, unsigned short port, unsigned short intport) {
	std::lock_guard<std::recursive_mutex> upnpGuard(upnpLock);
	upnpReqs.push_back({ UPNP_CMD_ADD, protocol, port, intport });
}

void UPnP_Remove(const char* protocol, unsigned short port) {
	std::lock_guard<std::recursive_mutex> upnpGuard(upnpLock);
	upnpReqs.push_back({ UPNP_CMD_REMOVE, protocol, port, port });
}

