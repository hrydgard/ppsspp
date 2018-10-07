// Copyright (c) 2014- PPSSPP Project.

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

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "base/stringutil.h"
#include "base/timeutil.h"
#include "file/fd_util.h"
#include "net/http_client.h"
#include "net/http_server.h"
#include "net/sinks.h"
#include "thread/threadutil.h"
#include "Common/FileUtil.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/WebServer.h"

enum class ServerStatus {
	STOPPED,
	STARTING,
	RUNNING,
	STOPPING,
	RESTARTING,
};

static const char *REPORT_HOSTNAME = "report.ppsspp.org";
static const int REPORT_PORT = 80;

static std::thread serverThread;
static ServerStatus serverStatus;
static std::mutex serverStatusLock;
static int serverFlags;

static void UpdateStatus(ServerStatus s) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	serverStatus = s;
}

static bool UpdateStatus(ServerStatus s, ServerStatus old) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus == old) {
		serverStatus = s;
		return true;
	}
	return false;
}

static ServerStatus RetrieveStatus() {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	return serverStatus;
}

// This reports the local IP address to report.ppsspp.org, which can then
// relay that address to a mobile device searching for the server.
static void RegisterServer(int port) {
	http::Client http;
	Buffer theVoid;

	char resource4[1024] = {};
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV4)) {
		if (http.Connect()) {
			std::string ip = fd_util::GetLocalIP(http.sock());
			snprintf(resource4, sizeof(resource4) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			http.GET(resource4, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}

	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV6)) {
		// We register both IPv4 and IPv6 in case the other client is using a different one.
		if (resource4[0] != 0 && http.Connect()) {
			http.GET(resource4, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}

		// Currently, we're not using keepalive, so gotta reconnect...
		if (http.Connect()) {
			char resource6[1024] = {};
			std::string ip = fd_util::GetLocalIP(http.sock());
			snprintf(resource6, sizeof(resource6) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			http.GET(resource6, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}
}

bool RemoteISOFileSupported(const std::string &filename) {
	// Disc-like files.
	if (endsWithNoCase(filename, ".cso") || endsWithNoCase(filename, ".iso")) {
		return true;
	}
	// May work - but won't have supporting files.
	if (endsWithNoCase(filename, ".pbp")) {
		return true;
	}
	// Debugging files.
	if (endsWithNoCase(filename, ".ppdmp")) {
		return true;
	}
	return false;

}

static void RegisterDiscHandlers(http::Server *http, std::unordered_map<std::string, std::string> *paths) {
	for (std::string filename : g_Config.recentIsos) {
#ifdef _WIN32
		static const std::string sep = "\\/";
#else
		static const std::string sep = "/";
#endif
		size_t basepos = filename.find_last_of(sep);
		std::string basename = "/" + (basepos == filename.npos ? filename : filename.substr(basepos + 1));

		// Let's not serve directories, since they won't work.  Only single files.
		// Maybe can do PBPs and other files later.  Would be neat to stream virtual disc filesystems.
		if (RemoteISOFileSupported(basename)) {
			(*paths)[ReplaceAll(basename, " ", "%20")] = filename;
		}
	}

	auto handler = [paths](const http::Request &request) {
		std::string filename = (*paths)[request.resource()];
		s64 sz = File::GetFileSize(filename);

		std::string range;
		if (request.Method() == http::RequestHeader::HEAD) {
			request.WriteHttpResponseHeader(200, sz, "application/octet-stream", "Accept-Ranges: bytes\r\n");
		} else if (request.GetHeader("range", &range)) {
			s64 begin = 0, last = 0;
			if (sscanf(range.c_str(), "bytes=%lld-%lld", &begin, &last) != 2) {
				request.WriteHttpResponseHeader(400, -1, "text/plain");
				request.Out()->Push("Could not understand range request.");
				return;
			}

			if (begin < 0 || begin > last || last >= sz) {
				request.WriteHttpResponseHeader(416, -1, "text/plain");
				request.Out()->Push("Range goes outside of file.");
				return;
			}

			FILE *fp = File::OpenCFile(filename, "rb");
			if (!fp || fseek(fp, begin, SEEK_SET) != 0) {
				request.WriteHttpResponseHeader(500, -1, "text/plain");
				request.Out()->Push("File access failed.");
				if (fp) {
					fclose(fp);
				}
				return;
			}

			s64 len = last - begin + 1;
			char contentRange[1024];
			sprintf(contentRange, "Content-Range: bytes %lld-%lld/%lld\r\n", begin, last, sz);
			request.WriteHttpResponseHeader(206, len, "application/octet-stream", contentRange);

			const size_t CHUNK_SIZE = 16 * 1024;
			char *buf = new char[CHUNK_SIZE];
			for (s64 pos = 0; pos < len; pos += CHUNK_SIZE) {
				s64 chunklen = std::min(len - pos, (s64)CHUNK_SIZE);
				if (fread(buf, chunklen, 1, fp) != 1)
					break;
				request.Out()->Push(buf, chunklen);
			}
			fclose(fp);
			delete [] buf;
			request.Out()->Flush();
		} else {
			request.WriteHttpResponseHeader(418, -1, "text/plain");
			request.Out()->Push("This server only supports range requests.");
		}
	};

	for (auto pair : *paths) {
		http->RegisterHandler(pair.first.c_str(), handler);
	}
}

static void ExecuteWebServer() {
	setCurrentThreadName("HTTPServer");

	auto http = new http::Server(new threading::NewThreadExecutor());
	std::unordered_map<std::string, std::string> discPaths;

	if (serverFlags & (int)WebServerFlags::DISCS) {
		RegisterDiscHandlers(http, &discPaths);
	}
	if (serverFlags & (int)WebServerFlags::DEBUGGER) {
		http->RegisterHandler("/debugger", &HandleDebuggerRequest);
	}

	if (!http->Listen(g_Config.iRemoteISOPort)) {
		if (!http->Listen(0)) {
			ERROR_LOG(FILESYS, "Unable to listen on any port");
			UpdateStatus(ServerStatus::STOPPED);
			return;
		}
	}
	UpdateStatus(ServerStatus::RUNNING);

	g_Config.iRemoteISOPort = http->Port();
	RegisterServer(http->Port());
	double lastRegister = real_time_now();
	while (RetrieveStatus() == ServerStatus::RUNNING) {
		http->RunSlice(1.0);

		double now = real_time_now();
		if (now > lastRegister + 540.0) {
			RegisterServer(http->Port());
			lastRegister = now;
		}
	}

	http->Stop();
	StopAllDebuggers();
	delete http;

	// Move to STARTING to lock flags/STOPPING.
	if (UpdateStatus(ServerStatus::STARTING, ServerStatus::RESTARTING)) {
		ExecuteWebServer();
	} else {
		UpdateStatus(ServerStatus::STOPPED);
	}
}

bool StartWebServer(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	switch (serverStatus) {
	case ServerStatus::RUNNING:
	case ServerStatus::RESTARTING:
		if ((serverFlags & (int)flags) == (int)flags) {
			// Already running those flags.
			return false;
		}
		serverStatus = ServerStatus::RESTARTING;
		serverFlags |= (int)flags;
		return true;

	case ServerStatus::STOPPED:
		serverStatus = ServerStatus::STARTING;
		serverFlags = (int)flags;
		serverThread = std::thread(&ExecuteWebServer);
		serverThread.detach();
		return true;

	default:
		return false;
	}
}

bool StopWebServer(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus != ServerStatus::RUNNING && serverStatus != ServerStatus::RESTARTING) {
		return false;
	}

	serverFlags &= ~(int)flags;
	if (serverFlags == 0) {
		serverStatus = ServerStatus::STOPPING;
	} else {
		serverStatus = ServerStatus::RESTARTING;
	}
	return true;
}

bool WebServerStopping(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus == ServerStatus::RESTARTING) {
		return (serverFlags & (int)flags) == 0;
	}
	return serverStatus == ServerStatus::STOPPING;
}

bool WebServerStopped(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus == ServerStatus::RUNNING) {
		return (serverFlags & (int)flags) == 0;
	}
	return serverStatus == ServerStatus::STOPPED;
}
