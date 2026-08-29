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

// Threading model: everything in PortManager runs on the UPnP service thread, which exists only
// while the UPnP setting is on. Other threads only ever push requests onto g_upnpReqs
// (UPnP_Add/UPnP_Remove) or poke the condition variable (UPnP_Notify). Talking to a router means
// blocking socket I/O with multi-second timeouts, so none of it may happen on a thread anyone
// waits for - and the thread must never end up in a state where it can't be told to stop.

#include <algorithm>  // find_if
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/OSD.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/PortManager.h"

PortManager g_PortManager;

// Guards g_upnpServiceThread itself, so that a start racing a start (or a shutdown) can't end up
// with two service threads or a std::thread being reassigned while it's still running.
static std::mutex g_upnpThreadLock;
static std::thread g_upnpServiceThread;
static bool g_upnpInitialized;

static std::mutex g_upnpLock;
static std::condition_variable g_upnpCond;

// All of the following are guarded by g_upnpLock.
static std::deque<UPnPArgs> g_upnpReqs;
static unsigned int g_upnpTimeout = 2000;
static bool g_upnpExit;
// Whether a service thread is up. The thread only exists while the setting is on, so most users
// never pay for one at all - it used to be started unconditionally and parked for the session.
static bool g_upnpThreadRunning;
// Bumped whenever there's a reason for the service thread to wake up and look around. The thread
// snapshots it before doing (slow) work, so a request that arrives while it's busy can't be missed
// and leave it asleep with something queued.
static uint32_t g_upnpWakeSeq;
// Set by UPnP_Notify() - the user flipped a setting, so retry immediately instead of continuing to
// back off from earlier failures.
static bool g_upnpResetBackoff;

// A game hammering bind() shouldn't be able to grow the queue without bound, especially while
// UPnP is unreachable and nothing is being drained.
static constexpr size_t MAX_QUEUED_REQUESTS = 64;
// After this many failed round trips we drop a request rather than let it block the queue forever.
static constexpr int MAX_REQUEST_ATTEMPTS = 3;
// Backoff bounds for rediscovering a router. Without a cap this used to redo a full SSDP discovery
// every five seconds, forever, for anyone who enabled UPnP without a router that supports it.
static constexpr double MIN_RETRY_SECONDS = 5.0;
static constexpr double MAX_RETRY_SECONDS = 300.0;
// Some routers do not support non-zero lease durations, in which case we fall back to 0 (permanent).
static const char * const DEFAULT_LEASE_DURATION = "43200";  // 12 hours, range is 0-604800 (0 = indefinite)

#ifdef WITH_UPNP
// Real routers report an error once the index runs past the end of the mapping table. This is just
// a backstop against one that never does - walking to 65536 would mean 65536 HTTP round trips.
static constexpr int MAX_PORT_MAPPINGS = 1024;

// Sizes miniupnpc copies into these buffers (see the strncpy calls in upnpcommands.c). It does not
// guarantee a terminating NUL, so we give every buffer one spare byte and zero it before each call.
struct PortMappingEntry {
	char extPort[6 + 1];
	char intClient[16 + 1];
	char intPort[6 + 1];
	char protocol[4 + 1];
	char desc[80 + 1];
	char enabled[4 + 1];
	char rHost[64 + 1];
	char duration[16 + 1];
};
#endif

static void ShowUPnPMessage(const char *key) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	g_OSD.Show(OSDType::MESSAGE_INFO, n->T(key), 0.0f, "upnp_warning");
}

void PortManager::Shutdown(double budgetSeconds) {
	if (m_InitState == UPNP_INITSTATE_DONE) {
		m_deadline = time_now_d() + budgetSeconds;
		Clear();
		Restore();
		m_deadline = 0.0;
	}
	Terminate();
}

void PortManager::Terminate() {
	DEBUG_LOG(Log::Net, "PortManager::Terminate()");
#ifdef WITH_UPNP
	if (m_urlsValid) {
		FreeUPNPUrls(&m_urls);
		m_urlsValid = false;
	}
#endif
	memset(&m_urls, 0, sizeof(m_urls));
	memset(&m_datas, 0, sizeof(m_datas));
	m_otherPortList.clear(); m_otherPortList.shrink_to_fit();
	m_portList.clear(); m_portList.shrink_to_fit();
	m_lanip.clear();
	m_leaseDuration = DEFAULT_LEASE_DURATION;
	m_LocalPort = UPNP_LOCAL_PORT_ANY;
	m_deadline = 0.0;
	m_InitState = UPNP_INITSTATE_NONE;
}

bool PortManager::HaveControlURL() const {
	return m_urlsValid && m_urls.controlURL && m_urls.controlURL[0] != '\0';
}

bool PortManager::OutOfTime() const {
	return m_deadline != 0.0 && time_now_d() > m_deadline;
}

bool PortManager::Initialize(unsigned int timeout) {
#ifdef WITH_UPNP
	DEBUG_LOG(Log::Net, "PortManager::Initialize(%d)", timeout);

	if (m_InitState == UPNP_INITSTATE_DONE) {
		return true;
	}

	// A previous failed attempt can leave allocations and half-filled structs behind, so always
	// start from a clean slate. This used to leak a whole UPNPUrls/IGDdatas pair per attempt.
	Terminate();
	m_InitState = UPNP_INITSTATE_BUSY;

	int error = 0;
	// m_LocalPort is UPNP_LOCAL_PORT_ANY (0), or UPNP_LOCAL_PORT_SAME (1) as an alias for 1900.
	const int ipv6 = 0;
	const unsigned char ttl = 2;
	UPNPDev *devlist = upnpDiscover(timeout, nullptr, nullptr, m_LocalPort, ipv6, ttl, &error);
	if (!devlist) {
		ERROR_LOG(Log::Net, "PortManager - upnpDiscover failed (error: %i) or no UPnP device detected", error);
		m_InitState = UPNP_INITSTATE_NONE;
		return false;
	}

	for (const UPNPDev *dev = devlist; dev; dev = dev->pNext) {
		INFO_LOG(Log::Net, "PortManager - UPnP device: [desc: %s] [st: %s]", dev->descURL, dev->st);
	}

	// UPNP_GetValidIGD downloads and parses the root descriptions itself and fills in both structs,
	// so there's no need to miniwget/parserootdesc/GetUPNPUrls up front - doing that only cost an
	// extra HTTP round trip to the router and leaked the URLs, since GetValidIGD memsets over them.
	//
	// possible "status" values:
	// -1 = Internal error
	//  0 = NO IGD found
	//  1 = A valid connected IGD has been found
	//  2 = A valid connected IGD has been found but its IP address is reserved (non routable)
	//  3 = A valid IGD has been found but it reported as not connected
	//  4 = an UPnP device has been found but was not recognized as an IGD
	char lanaddr[64] = "";
#if (MINIUPNPC_API_VERSION >= 18)
	const int status = UPNP_GetValidIGD(devlist, &m_urls, &m_datas, lanaddr, sizeof(lanaddr), nullptr, 0);
#else
	const int status = UPNP_GetValidIGD(devlist, &m_urls, &m_datas, lanaddr, sizeof(lanaddr));
#endif
	lanaddr[sizeof(lanaddr) - 1] = '\0';
	freeUPNPDevlist(devlist);
	m_urlsValid = status >= 1;

	// Without a control URL there's nothing we can ask the router to do, and pressing on would just
	// mean handing NULL to every UPNP_* call below.
	if (!HaveControlURL()) {
		ERROR_LOG(Log::Net, "PortManager - no usable IGD found (status=%d)", status);
		Terminate();
		return false;
	}
	if (status != 1) {
		WARN_LOG(Log::Net, "PortManager - IGD found but in an unexpected state (status=%d), trying anyway", status);
	}

	m_lanip = lanaddr;
	INFO_LOG(Log::Net, "PortManager - Detected LAN IP: %s (status=%d)", m_lanip.c_str(), status);

	// miniupnpc writes up to 64 bytes here and doesn't guarantee a terminator, hence the spare byte.
	char connectionType[64 + 1] = "";
	if (UPNP_GetConnectionTypeInfo(m_urls.controlURL, m_datas.first.servicetype, connectionType) != UPNPCOMMAND_SUCCESS) {
		WARN_LOG(Log::Net, "PortManager - GetConnectionTypeInfo failed");
	} else {
		INFO_LOG(Log::Net, "PortManager - Connection Type: %s", connectionType);
	}

	m_InitState = UPNP_INITSTATE_DONE;
	RefreshPortList();
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Add(const char *protocol, unsigned short port, unsigned short intport, const std::string &desc) {
#ifdef WITH_UPNP
	if (intport == 0)
		intport = port;
	INFO_LOG(Log::Net, "PortManager::Add(%s, %d, %d)", protocol, port, intport);
	if (!HaveControlURL()) {
		WARN_LOG(Log::Net, "PortManager::Add - the init was not done !");
		Terminate();
		return false;
	}

	char port_str[16];
	char intport_str[16];
	snprintf(port_str, sizeof(port_str), "%d", port);
	snprintf(intport_str, sizeof(intport_str), "%d", intport);

	// Nothing to do if we already mapped this port ourselves.
	if (std::find_if(m_portList.begin(), m_portList.end(), [port_str, protocol](const std::pair<std::string, std::string> &el) {
			return el.first == port_str && el.second == protocol;
		}) != m_portList.end()) {
		return true;
	}

	auto other = std::find_if(m_otherPortList.begin(), m_otherPortList.end(), [port_str, protocol](const PortMap &el) {
		return el.extPort_str == port_str && el.protocol == protocol;
	});
	if (other != m_otherPortList.end()) {
		// Someone else holds this port - drop their mapping first. Mark it as taken as soon as the
		// delete goes through, not once our own mapping is in place: if the add below fails we still
		// owe them a Restore(). This also covers a dangling mapping of ours from a session that
		// didn't shut down cleanly.
		if (UPNP_DeletePortMapping(m_urls.controlURL, m_datas.first.servicetype, port_str, protocol, nullptr) == 0)
			other->taken = true;
	}

	int r = UPNP_AddPortMapping(m_urls.controlURL, m_datas.first.servicetype,
		port_str, intport_str, m_lanip.c_str(), desc.c_str(), protocol, nullptr, m_leaseDuration.c_str());
	if (r == 725 && m_leaseDuration != "0") {
		// OnlyPermanentLeasesSupported - remember that for the rest of the session.
		m_leaseDuration = "0";
		r = UPNP_AddPortMapping(m_urls.controlURL, m_datas.first.servicetype,
			port_str, intport_str, m_lanip.c_str(), desc.c_str(), protocol, nullptr, m_leaseDuration.c_str());
	}
	if (r != 0) {
		ERROR_LOG(Log::Net, "PortManager - AddPortMapping failed (error: %i)", r);
		if (r == UPNPCOMMAND_HTTP_ERROR) {
			// Usually means the router is no longer reachable (changed networks, went to sleep).
			// Invalidate the state so we rediscover instead of eating a timeout on every request.
			ShowUPnPMessage("UPnP need to be reinitialized");
			Terminate();
			return false;
		}
		// Some other UPnP-level refusal. Report it as handled - retrying won't help.
		return true;
	}

	m_portList.push_front({ port_str, protocol });
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Remove(const char *protocol, unsigned short port) {
#ifdef WITH_UPNP
	INFO_LOG(Log::Net, "PortManager::Remove(%s, %d)", protocol, port);
	if (!HaveControlURL()) {
		WARN_LOG(Log::Net, "PortManager::Remove - the init was not done !");
		Terminate();
		return false;
	}

	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%d", port);
	int r = UPNP_DeletePortMapping(m_urls.controlURL, m_datas.first.servicetype, port_str, protocol, nullptr);
	if (r != 0) {
		ERROR_LOG(Log::Net, "PortManager - DeletePortMapping failed (error: %i)", r);
		if (r == UPNPCOMMAND_HTTP_ERROR) {
			ShowUPnPMessage("UPnP need to be reinitialized");
			Terminate();
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
	VERBOSE_LOG(Log::Net, "PortManager::Restore()");
	if (!HaveControlURL()) {
		WARN_LOG(Log::Net, "PortManager::Restore - the init was not done !");
		return false;
	}
	for (PortMap &entry : m_otherPortList) {
		if (!entry.taken)
			continue;
		if (OutOfTime()) {
			WARN_LOG(Log::Net, "PortManager::Restore - out of time, leaving the rest to the router's lease timeout");
			return false;
		}
		// Remove it first if it's still being held by PPSSPP.
		auto el_it = std::find_if(m_portList.begin(), m_portList.end(), [&entry](const std::pair<std::string, std::string> &el) {
			return el.first == entry.extPort_str && el.second == entry.protocol;
		});
		if (el_it != m_portList.end()) {
			int r = UPNP_DeletePortMapping(m_urls.controlURL, m_datas.first.servicetype, entry.extPort_str.c_str(), entry.protocol.c_str(), nullptr);
			if (r == 0) {
				m_portList.erase(el_it);
			} else {
				ERROR_LOG(Log::Net, "PortManager::Restore - DeletePortMapping failed (error: %i)", r);
				if (r == UPNPCOMMAND_HTTP_ERROR)
					return false;  // The router is gone, the rest of the loop would just be timeouts.
			}
		}
		// Add the original owner back.
		int r = UPNP_AddPortMapping(m_urls.controlURL, m_datas.first.servicetype,
			entry.extPort_str.c_str(), entry.intPort_str.c_str(), entry.lanip.c_str(), entry.desc.c_str(),
			entry.protocol.c_str(), entry.remoteHost.c_str(), entry.duration.c_str());
		if (r == 0) {
			entry.taken = false;
		} else {
			ERROR_LOG(Log::Net, "PortManager::Restore - AddPortMapping failed (error: %i)", r);
			if (r == UPNPCOMMAND_HTTP_ERROR)
				return false;
		}
	}
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::Clear() {
#ifdef WITH_UPNP
	VERBOSE_LOG(Log::Net, "PortManager::Clear()");
	if (!HaveControlURL()) {
		WARN_LOG(Log::Net, "PortManager::Clear - the init was not done !");
		return false;
	}
	// m_portList holds every mapping we know PPSSPP owns: RefreshPortList() seeds it at init time
	// (so leftovers from a session that crashed are in there), and Add/Remove keep it current. That
	// beats re-walking the router's whole table here, which cost an HTTP round trip per entry at
	// the worst possible moment - app exit.
	while (!m_portList.empty()) {
		if (OutOfTime()) {
			WARN_LOG(Log::Net, "PortManager::Clear - out of time, %d mapping(s) left for the router's lease timeout", (int)m_portList.size());
			return false;
		}
		const std::pair<std::string, std::string> &entry = m_portList.front();
		int r = UPNP_DeletePortMapping(m_urls.controlURL, m_datas.first.servicetype, entry.first.c_str(), entry.second.c_str(), nullptr);
		if (r != 0) {
			ERROR_LOG(Log::Net, "PortManager::Clear - DeletePortMapping(%s, %s) failed (error: %i)", entry.first.c_str(), entry.second.c_str(), r);
			if (r == UPNPCOMMAND_HTTP_ERROR)
				return false;
		}
		m_portList.pop_front();
	}
	return true;
#else
	return false;
#endif // WITH_UPNP
}

bool PortManager::RefreshPortList() {
#ifdef WITH_UPNP
	INFO_LOG(Log::Net, "PortManager::RefreshPortList()");
	if (!HaveControlURL()) {
		WARN_LOG(Log::Net, "PortManager::RefreshPortList - the init was not done !");
		return false;
	}
	m_portList.clear();
	m_otherPortList.clear();

	PortMappingEntry e;
	int i = 0;
	int r;
	do {
		if (OutOfTime())
			return false;
		char index[16];
		snprintf(index, sizeof(index), "%d", i);
		memset(&e, 0, sizeof(e));
		r = UPNP_GetGenericPortMappingEntry(m_urls.controlURL, m_datas.first.servicetype, index,
			e.extPort, e.intClient, e.intPort, e.protocol, e.desc, e.enabled, e.rHost, e.duration);
		if (r == 0) {
			std::string desc = e.desc;
			// Some routers prefix the description with "UPnP:", which we need to strip so it doesn't
			// accumulate another prefix each time we restore the mapping.
			if (startsWith(desc, "UPnP:"))
				desc = desc.substr(5);
			if (e.intClient == m_lanip && desc.find("PPSSPP:") != std::string::npos) {
				// Ours, possibly left over from an earlier session. Clear() will drop it.
				m_portList.push_back({ e.extPort, e.protocol });
			} else {
				// Someone else's, which we may end up taking over (and then have to put back).
				m_otherPortList.push_back({ false, e.protocol, e.extPort, e.intPort, e.intClient, e.rHost, desc, e.duration, e.enabled });
			}
		}
		i++;
	} while (r == 0 && i < MAX_PORT_MAPPINGS);

	if (i >= MAX_PORT_MAPPINGS)
		WARN_LOG(Log::Net, "PortManager::RefreshPortList - stopped after %d entries", MAX_PORT_MAPPINGS);
	INFO_LOG(Log::Net, "PortManager - %d existing PPSSPP mapping(s), %d belonging to others",
		(int)m_portList.size(), (int)m_otherPortList.size());
	return true;
#else
	return false;
#endif // WITH_UPNP
}

// --- Service thread ---

static void DiscardQueuedRequests() {
	std::lock_guard<std::mutex> lock(g_upnpLock);
	if (!g_upnpReqs.empty()) {
		DEBUG_LOG(Log::Net, "UPnPService: discarding %d queued request(s)", (int)g_upnpReqs.size());
		g_upnpReqs.clear();
	}
}

// Works through the queue until it's empty or the router stops answering.
// Returns false if we lost the router, in which case the unfinished request stays queued.
static bool ProcessQueuedRequests() {
	while (true) {
		UPnPArgs arg;
		{
			std::lock_guard<std::mutex> lock(g_upnpLock);
			if (g_upnpExit || g_upnpReqs.empty())
				return true;
			// Take it out of the queue for the duration. Talking to the router happens without the
			// lock held, and QueueRequest() supersedes pending requests for the same port - if the
			// in-flight one were still in the deque it could be erased out from under us, and we'd
			// then drop whatever replaced it without ever running it.
			arg = std::move(g_upnpReqs.front());
			g_upnpReqs.pop_front();
		}

		bool ok;
		switch (arg.cmd) {
		case UPNP_CMD_ADD:
			ok = g_PortManager.Add(arg.protocol.c_str(), arg.port, arg.intport, arg.desc);
			break;
		case UPNP_CMD_REMOVE:
			ok = g_PortManager.Remove(arg.protocol.c_str(), arg.port);
			break;
		default:
			ok = true;
			break;
		}
		if (ok)
			continue;

		// The router stopped answering, and Add()/Remove() have already reset us to disconnected.
		// Put the request back for after we reconnect and stop draining - everything behind it
		// would just fail the same way.
		if (++arg.attempts < MAX_REQUEST_ATTEMPTS) {
			std::lock_guard<std::mutex> lock(g_upnpLock);
			const bool superseded = std::any_of(g_upnpReqs.begin(), g_upnpReqs.end(), [&arg](const UPnPArgs &req) {
				return req.port == arg.port && req.protocol == arg.protocol;
			});
			if (!superseded && g_upnpReqs.size() < MAX_QUEUED_REQUESTS)
				g_upnpReqs.push_front(std::move(arg));
		} else {
			WARN_LOG(Log::Net, "UPnPService: giving up on %s port %d after %d attempts",
				arg.protocol.c_str(), arg.port, MAX_REQUEST_ATTEMPTS);
		}
		return false;
	}
}

static int upnpService(unsigned int timeout) {
	SetCurrentThreadName("UPnPService");
	INFO_LOG(Log::Net, "UPnPService: Begin of UPnPService Thread");

	int failCount = 0;
	bool wasEnabled = false;
	// Absolute time before which we won't try to (re)discover a router. This has to be a deadline
	// rather than a sleep length: an incoming request wakes us early, and without it every single
	// request would trigger another full SSDP discovery while the router is unreachable.
	double nextInitTime = 0.0;

	while (true) {
		uint32_t seq;
		{
			std::lock_guard<std::mutex> lock(g_upnpLock);
			if (g_upnpExit)
				break;
			seq = g_upnpWakeSeq;
			if (g_upnpResetBackoff) {
				// The user just flipped the setting - don't make them wait out an old backoff.
				g_upnpResetBackoff = false;
				failCount = 0;
				nextInitTime = 0.0;
			}
		}

		// 0 means "sleep until someone wakes us" - which is what this thread does for the whole
		// session for most users, rather than waking up periodically to find nothing to do.
		double wakeAt = 0.0;

		if (!g_Config.bEnableUPnP) {
			// Callers queue requests without checking the setting (see bind() in sceNetInet), so throw
			// them away here. Otherwise the queue grows without bound and, worse, the wait below never
			// blocks - which is what pegged a core whenever UPnP was off but a game was using sockets.
			DiscardQueuedRequests();
			if (wasEnabled) {
				// Turned off at runtime - take our mappings back down right away.
				INFO_LOG(Log::Net, "UPnPService: UPnP was disabled, cleaning up");
				g_PortManager.Shutdown();
				wasEnabled = false;
			}
			failCount = 0;
			nextInitTime = 0.0;

			std::lock_guard<std::mutex> lock(g_upnpLock);
			if (!g_Config.bEnableUPnP) {
				// Nothing left to do until the setting comes back on, and StartUPnPService() will
				// spin up a fresh thread for that. Deciding this under the lock is what makes that
				// safe: a start that observed us still running can't be left without a thread.
				g_upnpThreadRunning = false;
				break;
			}
			// Turned back on while we were cleaning up (which involves network round trips, so
			// there's real time in there) - carry on rather than exiting.
			continue;
		} else {
			wasEnabled = true;
			if (g_PortManager.GetInitState() == UPNP_INITSTATE_NONE && time_now_d() >= nextInitTime) {
				if (g_PortManager.Initialize(timeout)) {
					failCount = 0;
					nextInitTime = 0.0;
				} else {
					// Only complain the first time, not once per retry.
					if (failCount == 0)
						ShowUPnPMessage("Unable to find UPnP device");
					failCount++;
					const double backoff = std::min(MIN_RETRY_SECONDS * (1 << std::min(failCount - 1, 8)), MAX_RETRY_SECONDS);
					nextInitTime = time_now_d() + backoff;
				}
			}

			if (g_PortManager.GetInitState() == UPNP_INITSTATE_DONE) {
				if (!ProcessQueuedRequests()) {
					// Lost the router mid-request. Whatever's left stays queued for after we reconnect.
					nextInitTime = time_now_d() + MIN_RETRY_SECONDS;
				}
			}
			if (g_PortManager.GetInitState() != UPNP_INITSTATE_DONE)
				wakeAt = nextInitTime;
		}

		std::unique_lock<std::mutex> lock(g_upnpLock);
		auto shouldWake = [seq] { return g_upnpExit || g_upnpWakeSeq != seq; };
		if (wakeAt == 0.0) {
			g_upnpCond.wait(lock, shouldWake);
		} else {
			const double delay = std::max(wakeAt - time_now_d(), 0.0);
			g_upnpCond.wait_for(lock, std::chrono::duration<double>(delay), shouldWake);
		}
	}

	// Clean up regardless of g_Config.bEnableUPnP, to avoid leaving open ports on the router.
	g_PortManager.Shutdown();

	{
		std::lock_guard<std::mutex> lock(g_upnpLock);
		g_upnpReqs.clear();
	}

	INFO_LOG(Log::Net, "UPnPService: End of UPnPService Thread");
	return 0;
}

// Starts the service thread if the setting is on and it isn't up already.
static void StartUPnPService() {
	std::lock_guard<std::mutex> threadLock(g_upnpThreadLock);
	{
		std::lock_guard<std::mutex> lock(g_upnpLock);
		if (!g_upnpInitialized || g_upnpExit || !g_Config.bEnableUPnP)
			return;
		if (g_upnpThreadRunning) {
			// Already up - it'll notice whatever changed on its own.
			return;
		}
		g_upnpThreadRunning = true;
		g_upnpResetBackoff = true;
	}
	// A previous thread may have exited when the setting was turned off. By the time it clears
	// g_upnpThreadRunning it has already cleaned up after itself, so this doesn't block on a router.
	if (g_upnpServiceThread.joinable())
		g_upnpServiceThread.join();
	g_upnpServiceThread = std::thread(upnpService, g_upnpTimeout);
}

void __UPnPInit(unsigned int timeout) {
	{
		std::lock_guard<std::mutex> lock(g_upnpLock);
		g_upnpExit = false;
		g_upnpReqs.clear();
		g_upnpInitialized = true;
		g_upnpTimeout = timeout;
	}
	// Only actually spawns a thread if UPnP is enabled; otherwise UPnP_Notify() starts one when
	// the user turns it on.
	StartUPnPService();
}

void __UPnPShutdown() {
	{
		std::lock_guard<std::mutex> lock(g_upnpLock);
		g_upnpInitialized = false;
		g_upnpExit = true;
		// Anything still queued is moot, and dropping it here means the thread won't try to talk to
		// a router we may no longer be able to reach on its way out.
		g_upnpReqs.clear();
		g_upnpWakeSeq++;
	}
	g_upnpCond.notify_all();

	std::lock_guard<std::mutex> threadLock(g_upnpThreadLock);
	if (g_upnpServiceThread.joinable()) {
		INFO_LOG(Log::Net, "Waiting for upnp thread to shut down...");
		g_upnpServiceThread.join();
		INFO_LOG(Log::Net, "upnp thread shut down.");
	}
	std::lock_guard<std::mutex> lock(g_upnpLock);
	g_upnpThreadRunning = false;
}

static void QueueRequest(UPnPArgs args) {
	// The enable setting can change after startup without the settings UI being involved - a
	// per-game config, or a libretro core option. Reconcile here rather than requiring every place
	// that can flip it to remember to call UPnP_Notify(): this starts a thread if the setting is on
	// and there isn't one, and is a cheap no-op otherwise. The other direction takes care of itself,
	// since queuing below wakes a thread that then notices the setting is off and cleans up.
	StartUPnPService();

	std::lock_guard<std::mutex> lock(g_upnpLock);
	// With UPnP off there's no service thread to drain the queue, so don't let one build up.
	if (!g_upnpThreadRunning || g_upnpExit)
		return;

	// Last request wins for a given port: this collapses the repeated adds that games produce when
	// they rebind in a loop, and lets a remove cancel an add that hasn't been sent yet.
	for (auto it = g_upnpReqs.begin(); it != g_upnpReqs.end(); ) {
		(it->port == args.port && it->protocol == args.protocol) ? it = g_upnpReqs.erase(it) : ++it;
	}
	if (g_upnpReqs.size() >= MAX_QUEUED_REQUESTS) {
		WARN_LOG(Log::Net, "UPnP request queue is full, dropping request for %s port %d", args.protocol.c_str(), args.port);
		return;
	}

	g_upnpReqs.push_back(std::move(args));
	g_upnpWakeSeq++;
	g_upnpCond.notify_one();
}

// Built here rather than on the service thread, which can't safely read the game state.
// Some routers automatically prefix the description with "UPnP:".
static std::string MappingDescription() {
	if (PSP_IsInited()) {
		return "PPSSPP:" + g_paramSFO.GetDiscID() + ":" + g_Config.sNickName;
	}
	return "PPSSPP:at_menu:" + g_Config.sNickName;
}

void UPnP_Add(const char *protocol, unsigned short port, unsigned short intport) {
	QueueRequest({ UPNP_CMD_ADD, protocol, port, intport ? intport : port, MappingDescription() });
}

void UPnP_Remove(const char *protocol, unsigned short port) {
	QueueRequest({ UPNP_CMD_REMOVE, protocol, port, port });
}

void UPnP_Notify() {
	{
		std::lock_guard<std::mutex> lock(g_upnpLock);
		g_upnpWakeSeq++;
		g_upnpResetBackoff = true;
	}
	g_upnpCond.notify_one();
	// Turned on: start a thread. Turned off: the running one cleans up and exits by itself, which
	// we deliberately don't wait for here - this is called from the UI thread, and the cleanup
	// means talking to a router that may be slow to answer.
	StartUPnPService();
}
