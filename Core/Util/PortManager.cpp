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


#include <string>
#include <Core/System.h>
#include <Core/Host.h>
#include <Core/ELF/ParamSFO.h>
#include "Core/HLE/proAdhoc.h" // This import is only used to get product id that was used to connect to adhoc server
#include "Core/Util/PortManager.h"
#include "i18n/i18n.h"


PortManager g_PortManager;

PortManager::PortManager(): 
	urls(0), 
	datas(0), 
	m_InitState(UPNP_INITSTATE_NONE),
	m_LocalPort(UPNP_LOCAL_PORT_ANY),
	m_leaseDuration("43200") {
}

PortManager::~PortManager() {
	Clear();
	Restore();
	Deinit();
}

void PortManager::Deinit() {
	if (urls) {
		FreeUPNPUrls(urls);
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
	m_InitState = UPNP_INITSTATE_DONE;
}

bool PortManager::Init(const unsigned int timeout) {
	// Windows: Assuming WSAStartup already called beforehand
	struct UPNPDev* devlist;
	struct UPNPDev* dev;
	char* descXML;
	int descXMLsize = 0;
	int descXMLstatus = 0;
	int localport = m_LocalPort; // UPNP_LOCAL_PORT_ANY (0), or UPNP_LOCAL_PORT_SAME (1) as an alias for 1900 for backwards compatability?
	int ipv6 = 0; // 0 = IPv4, 1 = IPv6
	unsigned char ttl = 2; // defaulting to 2
	int error = 0;
	
	INFO_LOG(SCENET, "PortManager::Init(%d)", timeout);
	if (!g_Config.bEnableUPnP) {
		ERROR_LOG(SCENET, "PortManager::Init - UPnP is Disabled on Networking Settings");
		return false;
	}

	if (m_InitState != UPNP_INITSTATE_NONE) {
		switch (m_InitState)
		{
		case UPNP_INITSTATE_BUSY: {
			WARN_LOG(SCENET, "PortManager - Initialization already in progress");
			return false;
		}
		// We should redetect UPnP just in case the player switched to a different network in the middle
		/*case UPNP_INITSTATE_DONE: {
			WARN_LOG(SCENET, "PortManager - Already Initialized");
			return false;
		}
		*/
		default:
			break;
		}
	}
	m_leaseDuration = "43200"; // 12 hours
	m_InitState = UPNP_INITSTATE_BUSY;
	urls = (UPNPUrls*)malloc(sizeof(struct UPNPUrls));
	datas = (IGDdatas*)malloc(sizeof(struct IGDdatas));
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

		INFO_LOG(SCENET, "PortManager - UPnP device: [desc: %s] [st: %s]", dev->descURL, dev->st);

		descXML = (char*)miniwget(dev->descURL, &descXMLsize, dev->scope_id, &descXMLstatus);
		if (descXML)
		{
			parserootdesc(descXML, descXMLsize, datas);
			free(descXML); descXML = 0;
			GetUPNPUrls(urls, datas, dev->descURL, dev->scope_id);
		}

		// Get LAN IP address that connects to the router
		char lanaddr[64] = "unset";
		int status = UPNP_GetValidIGD(devlist, urls, datas, lanaddr, sizeof(lanaddr)); //possible "status" values, 0 = NO IGD found, 1 = A valid connected IGD has been found, 2 = A valid IGD has been found but it reported as not connected, 3 = an UPnP device has been found but was not recognized as an IGD
		m_lanip = std::string(lanaddr);
		INFO_LOG(SCENET, "PortManager - Detected LAN IP: %s", m_lanip.c_str());

		// Additional Info
		char connectionType[64] = "";
		if (UPNP_GetConnectionTypeInfo(urls->controlURL, datas->first.servicetype, connectionType) != UPNPCOMMAND_SUCCESS) {
			WARN_LOG(SCENET, "PortManager - GetConnectionTypeInfo failed");
		}
		else {
			INFO_LOG(SCENET, "PortManager - Connection Type: %s", connectionType);
		}

		// Using Game ID & Player Name as default description for mapping (prioritizing the ID sent by the game to Adhoc server)
		char productid[10] = { 0 };
		memcpy(productid, product_code.data, sizeof(product_code.data));
		std::string gameID = std::string(productid);
		if (productid[0] == '\0') {
			gameID = g_paramSFO.GetDiscID();
		}
		m_defaultDesc = "PPSSPP:" + gameID + ":" + g_Config.sNickName;

		freeUPNPDevlist(devlist);

		//m_LocalPort = localport; // We shouldn't keep the right port for the next game reset if we wanted to redetect UPnP
		m_InitState = UPNP_INITSTATE_DONE;
		RefreshPortList();
		return true;
	}
	ERROR_LOG(SCENET, "PortManager - upnpDiscover failed (error: %i) or No UPnP device detected", error);
	auto n = GetI18NCategory("Networking");
	host->NotifyUserMessage(n->T("Unable to find UPnP device"), 6.0f, 0x0000ff);
	m_InitState = UPNP_INITSTATE_NONE;
	return false;
}

int PortManager::GetInitState() {
	return m_InitState;
}

bool PortManager::Add(unsigned short port, const char* protocol) {
	char port_str[16];
	int r;
	
	INFO_LOG(SCENET, "PortManager::Add(%d, %s)", port, protocol);
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(SCENET, "PortManager::Add - the init was not done !");
		return false;
	}
	sprintf(port_str, "%d", port);
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
			port_str, port_str, m_lanip.c_str(), m_defaultDesc.c_str(), protocol, NULL, m_leaseDuration.c_str());
		if (r == 725 && m_leaseDuration != "0") {
			m_leaseDuration = "0";
			r = UPNP_AddPortMapping(urls->controlURL, datas->first.servicetype,
				port_str, port_str, m_lanip.c_str(), m_defaultDesc.c_str(), protocol, NULL, m_leaseDuration.c_str());
		}
		if (r != 0)
		{
			ERROR_LOG(SCENET, "PortManager - AddPortMapping failed (error: %i)", r);
			if (r == UPNPCOMMAND_HTTP_ERROR) {
				auto n = GetI18NCategory("Networking");
				host->NotifyUserMessage(n->T("UPnP need to be reinitialized"), 6.0f, 0x0000ff);
				Deinit(); // Most of the time errors occurred because the router is no longer reachable (ie. changed networks) so we should invalidate the state to prevent further lags due to timeouts
				return false;
			}
		}
		m_portList.push_front({ port_str, protocol });
		// Keep tracks of it to be restored later if it belongs to others
		if (el_it != m_otherPortList.end()) el_it->taken = true;
	}
	return true;
}

bool PortManager::Remove(unsigned short port, const char* protocol) {
	char port_str[16];

	INFO_LOG(SCENET, "PortManager::Remove(%d, %s)", port, protocol);
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(SCENET, "PortManager::Remove - the init was not done !");
		return false;
	}
	sprintf(port_str, "%d", port);
	int r = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, port_str, protocol, NULL);
	if (r != 0)
	{
		ERROR_LOG(SCENET, "PortManager - DeletePortMapping failed (error: %i)", r);
		if (r == UPNPCOMMAND_HTTP_ERROR) {
			auto n = GetI18NCategory("Networking");
			host->NotifyUserMessage(n->T("UPnP need to be reinitialized"), 6.0f, 0x0000ff);
			Deinit(); // Most of the time errors occurred because the router is no longer reachable (ie. changed networks) so we should invalidate the state to prevent further lags due to timeouts
			return false;
		}
	}
	for (auto it = m_portList.begin(); it != m_portList.end(); ) {
		(it->first == port_str && it->second == protocol) ? it = m_portList.erase(it) : ++it;
	}
	return true;
}

bool PortManager::Restore() {
	int r;
	INFO_LOG(SCENET, "PortManager::Restore()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(SCENET, "PortManager::Remove - the init was not done !");
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
					ERROR_LOG(SCENET, "PortManager::Restore - DeletePortMapping failed (error: %i)", r);
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
				ERROR_LOG(SCENET, "PortManager::Restore - AddPortMapping failed (error: %i)", r);
				if (r == UPNPCOMMAND_HTTP_ERROR)
					return false; // Might be better not to exit here, but exiting a loop will avoid long timeouts in the case the router is no longer reachable
			}		
		}
	}
	return true;
}

bool PortManager::Clear() {
	int r;
	int i = 0;
	char index[6];
	char intAddr[40];
	char intPort[6];
	char extPort[6];
	char protocol[4];
	char desc[80];
	char enabled[6];
	char rHost[64];
	char duration[16];

	INFO_LOG(SCENET, "PortManager::Clear()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(SCENET, "PortManager::Clear - the init was not done !");
		return false;
	}
	//unsigned int num = 0;
	//UPNP_GetPortMappingNumberOfEntries(urls->controlURL, datas->first.servicetype, &num); // Not supported by many routers
	do {
		snprintf(index, 6, "%d", i);
		rHost[0] = '\0'; enabled[0] = '\0';
		duration[0] = '\0'; desc[0] = '\0';
		extPort[0] = '\0'; intPort[0] = '\0'; intAddr[0] = '\0';
		r = UPNP_GetGenericPortMappingEntry(urls->controlURL,
			datas->first.servicetype,
			index,
			extPort, intAddr, intPort,
			protocol, desc, enabled,
			rHost, duration);
		// Only removes port mappings created by PPSSPP for current LAN IP
		if (r == 0 && intAddr == m_lanip && std::strncmp(desc, "PPSSPP", 6) == 0) {
			int r2 = UPNP_DeletePortMapping(urls->controlURL, datas->first.servicetype, extPort, protocol, rHost);
			if (r2 != 0)
			{
				ERROR_LOG(SCENET, "PortManager::Clear - DeletePortMapping(%s, %s) failed (error: %i)", extPort, protocol, r2);
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
	} while (r == 0);
	return true;
}

bool PortManager::RefreshPortList() {
	int r;
	int i = 0;
	char index[6];
	char intAddr[40];
	char intPort[6];
	char extPort[6];
	char protocol[4];
	char desc[80];
	char enabled[6];
	char rHost[64];
	char duration[16];

	INFO_LOG(SCENET, "PortManager::RefreshPortList()");
	if (urls == NULL || urls->controlURL == NULL || urls->controlURL[0] == '\0')
	{
		if (g_Config.bEnableUPnP) WARN_LOG(SCENET, "PortManager::RefreshPortList - the init was not done !");
		return false;
	}
	m_portList.clear();
	m_otherPortList.clear();
	//unsigned int num = 0;
	//UPNP_GetPortMappingNumberOfEntries(urls->controlURL, datas->first.servicetype, &num); // Not supported by many routers
	do {
		snprintf(index, 6, "%d", i);
		rHost[0] = '\0'; enabled[0] = '\0';
		duration[0] = '\0'; desc[0] = '\0';
		extPort[0] = '\0'; intPort[0] = '\0'; intAddr[0] = '\0';
		r = UPNP_GetGenericPortMappingEntry(urls->controlURL,
			datas->first.servicetype,
			index,
			extPort, intAddr, intPort,
			protocol, desc, enabled,
			rHost, duration);
		if (r == 0) {
			// Only include port mappings created by PPSSPP for current LAN IP
			if (intAddr == m_lanip && std::strncmp(desc, "PPSSPP", 6) == 0) {
				m_portList.push_back({ extPort, protocol });
			}
			// Port mappings belong to others that might be taken by PPSSPP later
			else {
				m_otherPortList.push_back({ false, protocol, extPort, intPort, intAddr, rHost, desc, duration, enabled });
			}
		}
		i++;
	} while (r == 0);
	return true;
}
