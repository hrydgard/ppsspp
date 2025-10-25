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
#include <string_view>

#include "Common/Net/HTTPClient.h"
#include "Common/Net/HTTPServer.h"
#include "Common/Net/Sinks.h"
#include "Common/Net/URL.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/FileDescriptor.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Config.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/WebServer.h"

enum class ServerStatus {
	STOPPED,
	STARTING,
	RUNNING,
	STOPPING,
	FINISHED,
};

static const char *REPORT_HOSTNAME = "report.ppsspp.org";
static const int REPORT_PORT = 80;

static std::thread serverThread;
static ServerStatus serverStatus;
static std::mutex serverStatusLock;
static WebServerFlags serverFlags;

std::mutex g_webServerLock;
static Path g_uploadPath;  // TODO: Supply this through registration instead.

// NOTE: These *only* encode spaces, which is almost enough.

std::string ServerUriEncode(std::string_view plain) {
	return ReplaceAll(plain, " ", "%20");
}

std::string ServerUriDecode(std::string_view encoded) {
	return ReplaceAll(encoded, "%20", " ");
}

static void UpdateStatus(ServerStatus s) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	serverStatus = s;
}

static ServerStatus RetrieveStatus() {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	return serverStatus;
}

// This reports the local IP address to report.ppsspp.org, which can then
// relay that address to a mobile device on the same wifi/LAN searching for the server.
static bool RegisterServer(int port) {
	bool success = false;
	http::Client http(nullptr);
	bool cancelled = false;
	net::RequestProgress progress(&cancelled);
	Buffer theVoid = Buffer::Void();

	http.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

	char resource4[1024]{};
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV4)) {
		if (http.Connect()) {
			std::string ip = http.GetLocalIpAsString();
			snprintf(resource4, sizeof(resource4) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			if (http.GET(http::RequestParams(resource4), &theVoid, &progress) > 0)
				success = true;
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}

	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV6)) {
		// If IPv4 was successful, don't give this as much time (it blocks and sometimes IPv6 is broken.)
		double timeout = success ? 2.0 : 10.0;

		// We register both IPv4 and IPv6 in case the other client is using a different one.
		if (resource4[0] != 0 && http.Connect(timeout)) {
			if (http.GET(http::RequestParams(resource4), &theVoid, &progress) > 0)
				success = true;
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}

		// Currently, we're not using keepalive, so gotta reconnect...
		if (http.Connect(timeout)) {
			char resource6[1024] = {};
			std::string ip = http.GetLocalIpAsString();
			snprintf(resource6, sizeof(resource6) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			if (http.GET(http::RequestParams(resource6), &theVoid, &progress) > 0)
				success = true;
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}

	return success;
}

bool RemoteISOFileSupported(const std::string &filename) {
	// Disc-like files.
	if (endsWithNoCase(filename, ".cso") || endsWithNoCase(filename, ".iso") || endsWithNoCase(filename, ".chd")) {
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

static std::string RemotePathForRecent(const std::string &filename) {
	Path path(filename);
	if (path.Type() == PathType::HTTP) {
		// Don't re-share HTTP files from some other device.
		return std::string();
	}

#ifdef _WIN32
	static const std::string sep = "\\/";
#else
	static const std::string sep = "/";
#endif
	size_t basepos = filename.find_last_of(sep);
	std::string basename = "/" + (basepos == filename.npos ? filename : filename.substr(basepos + 1));

	if (basename == "/EBOOT.PBP") {
		// Go up one more folder.
		size_t nextpos = filename.find_last_of(sep, basepos - 1);
		basename = "/" + (nextpos == filename.npos ? filename : filename.substr(nextpos + 1));
	}

	// Let's not serve directories, since they won't work.  Only single files.
	// Maybe can do PBPs and other files later.  Would be neat to stream virtual disc filesystems.
	if (RemoteISOFileSupported(basename)) {
		return ServerUriEncode(basename);
	}

	return std::string();
}

static Path LocalFromRemotePath(std::string_view path) {
	switch ((RemoteISOShareType)g_Config.iRemoteISOShareType) {
	case RemoteISOShareType::RECENT:
		for (const std::string &filename : g_recentFiles.GetRecentFiles()) {
			std::string basename = RemotePathForRecent(filename);
			if (basename == path) {
				return Path(filename);
			}
		}
		return Path();
	case RemoteISOShareType::LOCAL_FOLDER:
	{
		std::string decoded = ServerUriDecode(path);

		if (decoded.empty() || decoded.front() != '/') {
			return Path();
		}

		// First reject backslashes, in case of any Windows shenanigans.
		if (decoded.find('\\') != std::string::npos) {
			return Path();
		}
		// Then, reject slashes combined with ".." to prevent directory traversal. Hope this is enough.
		if (decoded.find("/..") != std::string::npos) {
			return Path();
		}
		return Path(g_Config.sRemoteISOSharedDir) / decoded;
	}
	default:
		return Path();
	}
}

static void DiscHandler(const http::ServerRequest &request, const Path &filename) {
	s64 sz = File::GetFileSize(filename);
	if (sz == 0) {
		// Probably failed
		request.WriteHttpResponseHeader("1.0", 404, -1, "text/plain");
		request.Out()->Push("File not found.");
		return;
	}

	std::string range;
	if (request.Method() == http::RequestHeader::HEAD) {
		request.WriteHttpResponseHeader("1.0", 200, sz, "application/octet-stream", "Accept-Ranges: bytes\r\n");
	} else if (request.GetHeader("range", &range)) {
		s64 begin = 0, last = 0;
		if (sscanf(range.c_str(), "bytes=%lld-%lld", &begin, &last) != 2) {
			request.WriteHttpResponseHeader("1.0", 400, -1, "text/plain");
			request.Out()->Push("Could not understand range request.");
			return;
		}

		if (begin < 0 || begin > last || last >= sz) {
			request.WriteHttpResponseHeader("1.0", 416, -1, "text/plain");
			request.Out()->Push("Range goes outside of file.");
			return;
		}

		FILE *fp = File::OpenCFile(filename, "rb");
		if (!fp || fseek(fp, begin, SEEK_SET) != 0) {
			request.WriteHttpResponseHeader("1.0", 500, -1, "text/plain");
			request.Out()->Push("File access failed.");
			if (fp) {
				fclose(fp);
			}
			return;
		}

		s64 len = last - begin + 1;
		char contentRange[1024];
		snprintf(contentRange, sizeof(contentRange), "Content-Range: bytes %lld-%lld/%lld\r\n", begin, last, sz);
		request.WriteHttpResponseHeader("1.0", 206, len, "application/octet-stream", contentRange);

		const size_t CHUNK_SIZE = 16 * 1024;
		char *buf = new char[CHUNK_SIZE];
		for (s64 pos = 0; pos < len; pos += CHUNK_SIZE) {
			s64 chunklen = std::min(len - pos, (s64)CHUNK_SIZE);
			if (fread(buf, chunklen, 1, fp) != 1)
				break;
			request.Out()->Push(buf, chunklen);
		}
		fclose(fp);
		delete[] buf;
		request.Out()->Flush();
	} else {
		request.WriteHttpResponseHeader("1.0", 418, -1, "text/plain");
		request.Out()->Push("This server only supports range requests.");
	}
}

static void HandleListing(const http::ServerRequest &request) {
	AndroidJNIThreadContext jniContext;

	request.WriteHttpResponseHeader("1.0", 200, -1, "text/plain");
	request.Out()->Printf("/\n");
	if (serverFlags & WebServerFlags::DISCS) {
		switch ((RemoteISOShareType)g_Config.iRemoteISOShareType) {
		case RemoteISOShareType::RECENT:
			// List the current discs in their recent order.
			for (const std::string &filename : g_recentFiles.GetRecentFiles()) {
				std::string basename = RemotePathForRecent(filename);
				if (!basename.empty()) {
					request.Out()->Printf("%s\n", basename.c_str());
				}
			}
			break;
		case RemoteISOShareType::LOCAL_FOLDER:
		{
			std::vector<File::FileInfo> entries;

			std::string resource(request.resource());
			Path localDir = LocalFromRemotePath(resource);

			File::GetFilesInDir(localDir, &entries);
			for (const auto &entry : entries) {
				// TODO: Support browsing into subdirs. How are folders marked?
				if (!entry.isDirectory && !RemoteISOFileSupported(entry.name)) {
					continue;
				}
				std::string name = entry.name;
				if (entry.isDirectory) {
					name.push_back('/');
				}
				std::string encoded = ServerUriEncode(name); 
				request.Out()->Printf("%s\n", encoded.c_str());
			}
			break;
		}
		}
	}
	if (serverFlags & WebServerFlags::DEBUGGER) {
		request.Out()->Printf("/debugger\n");
	}
}

static bool ServeAssetFile(const http::ServerRequest &request) {
	// Skip the slash at the start of the resource path.
	std::string_view filename = request.resource().substr(1);
	if (filename.find("..") != std::string_view::npos) {
		// Don't allow directory traversal.
		return false;
	}

	size_t size;
	// TODO: ReadFile should take a string_view.
	uint8_t *data = g_VFS.ReadFile(std::string(filename).c_str(), &size);
	if (!data) {
		// Try appending index.html
		data = g_VFS.ReadFile((std::string(filename) + "/index.html").c_str(), &size);
		if (!data) {
			return false;
		}
		INFO_LOG(Log::HTTP, "Redirected to /index.html");
	}

	std::string ext = Path(filename).GetFileExtension();
	const char *mimeType = "text/plain";
	if (ext == ".html") {
		mimeType = "text/html";
	} else if (ext == ".ico") {
		mimeType = "image/x-icon";
	} else if (ext == ".js") {
		mimeType = "application/javascript";
	} else if (ext == ".svg") {
		mimeType = "image/svg+xml";
	} else if (ext == ".png") {
		mimeType = "image/png";
	} else if (ext == ".css") {
		mimeType = "text/css";
	}

	request.WriteHttpResponseHeader("1.0", 200, size, mimeType);
	request.Out()->Push((char *)data, size);

	delete[] data;
	return true;
}

static void RedirectToDebugger(const http::ServerRequest &request) {
	static const std::string payload = "Redirecting to debugger UI...\r\n";
	request.WriteHttpResponseHeader("1.0", 301, payload.size(), "text/plain", "Location: /debugger/index.html\r\n");
	request.Out()->Push(payload);
}

// TODO: Allow registering ServeAssetFile roots as well.
static void HandleFallback(const http::ServerRequest &request) {
	SetCurrentThreadName("HandleFallback");

	AndroidJNIThreadContext jniContext;

	if ((serverFlags & WebServerFlags::DEBUGGER) != 0) {
		if (request.resource() == "/debugger/") {
			RedirectToDebugger(request);
			return;
		}

		// Actually serve debugger files.
		if (startsWith(request.resource(), "/debugger/")) {
			if (ServeAssetFile(request)) {
				return;
			}
		}
	}

	if (serverFlags & WebServerFlags::FILE_UPLOAD) {
		if (startsWith(request.resource(), "/upload/")) {
			if (ServeAssetFile(request)) {
				return;
			}
		}
	}

	if (serverFlags & WebServerFlags::DISCS) {
		std::string_view resource = request.resource();
		Path localPath = LocalFromRemotePath(resource);
		INFO_LOG(Log::Loader, "Serving %.*s from %s", (int)resource.size(), resource.data(), localPath.c_str());
		if (!localPath.empty()) {
			if (File::IsDirectory(localPath)) {
				HandleListing(request);
			} else {
				DiscHandler(request, localPath);
			}
			return;
		}
	}

	static const std::string payload = "404 not found\r\n";
	request.WriteHttpResponseHeader("1.0", 404, payload.size(), "text/plain");
	request.Out()->Push(payload);
}

static void ForwardDebuggerRequest(const http::ServerRequest &request) {
	SetCurrentThreadName("ForwardDebuggerRequest");

	// Hm, is this needed?
	AndroidJNIThreadContext jniContext;

	if (serverFlags & WebServerFlags::DEBUGGER) {
		// Check if this is a websocket request...
		std::string upgrade;
		if (!request.GetHeader("upgrade", &upgrade)) {
			upgrade.clear();
		}

		// Yes - proceed with the socket.
		if (strcasecmp(upgrade.c_str(), "websocket") == 0) {
			HandleDebuggerRequest(request);
		} else {
			RedirectToDebugger(request);
		}
	} else {
		HandleFallback(request);
	}
}

static void HandleUploadUI(const http::ServerRequest &request) {
	// Read the file from VFS.
	AndroidJNIThreadContext jniContext;
	request.WriteHttpResponseHeader("1.0", 200, -1, "text/html");
	request.Out()->Push(
		"<html><head><title>PPSSPP Remote ISO Upload</title></head><body>"
		"<h1>Upload ISO File</h1>"
		"<form method=\"POST\" enctype=\"multipart/form-data\">"
		"<input type=\"file\" name=\"isofile\" accept=\".iso,.cso,.pbp,.chd\" required>"
		"<input type=\"submit\" value=\"Upload\">"
		"</form>"
		"</body></html>");
}

static void HandleUploadPost(const http::ServerRequest &request) {
	AndroidJNIThreadContext jniContext;

	// Do some sanity checks.
	if (request.Method() != http::RequestHeader::POST) {
		ERROR_LOG(Log::HTTP, "Wrong method");
		return;
	}

	Path uploadPath;
	{
		std::lock_guard<std::mutex> guard(g_webServerLock);
		uploadPath = g_uploadPath;
	}

	// Now start handling things.
	std::string contentType;
	if (!request.GetHeader("content-type", &contentType)) {
		return;
	}
	size_t bpos = contentType.find("boundary=");
	if (bpos == std::string::npos) {
		return;
	}
	const std::string boundary = contentType.substr(bpos + strlen("boundary="));

	// The total length of the entire multipart thing.
	u64 contentLength = request.Header().content_length;
	if (contentLength == 0) {
		WARN_LOG(Log::HTTP, "Bad content length");
		return;
	}

	std::string firstBoundary = request.In()->ReadLine();
	if (firstBoundary != "--" + boundary) {
		WARN_LOG(Log::HTTP, "Bad boundary: Expected --%s but got %s", boundary);
		return;
	}

	std::string disposition = request.In()->ReadLine();
	std::vector<std::string_view> parts;
	SplitString(disposition, ';', parts);
	if (parts.size() < 2 || !startsWith(parts[0], "Content-Disposition: form-data")) {
		WARN_LOG(Log::HTTP, "Bad content disposition: %s", disposition);
		return;
	}

	std::string filename;

	for (const auto &part : parts) {
		std::string_view key;
		std::string_view value;
		if (SplitStringOnce(part, &key, &value, '=')) {
			key = StripSpaces(key);
			value = StripQuotes(StripSpaces(value));
			if (key == "name") {
				INFO_LOG(Log::HTTP, "Upload field name: %.*s", STR_VIEW(value));
			} else if (key == "filename") {
				INFO_LOG(Log::HTTP, "Upload filename: %.*s", STR_VIEW(value));
				filename = value;
			}
		} else if (equalsNoCase(StripSpaces(part), "Content-Disposition: form-data")) {
			// this is the first part, ok, ignore.
		} else {
			WARN_LOG(Log::HTTP, "Bad content disposition part: %.*s", STR_VIEW(part));
		}
	}

	if (filename.empty()) {
		ERROR_LOG(Log::HTTP, "Didn't receive a filename");
		return;
	}

	std::string fileContentType = request.In()->ReadLine();
	std::string secondBoundary = request.In()->ReadLine();

	Path destPath = uploadPath / filename;

	if (File::Exists(destPath)) {
		INFO_LOG(Log::HTTP, "File already exists: %s", destPath.ToVisualString().c_str());
		return;
	}

	// Make sure the destination exists.
	File::CreateFullPath(destPath.NavigateUp());

	INFO_LOG(Log::HTTP, "Receiving '%s', writing to '%s' (%d bytes)...", filename.c_str(), destPath.ToVisualString().c_str());

	// OK, enter a loop where we read some data until we hit the boundary again.
	// The boundary is chosen to be "unique" and unlikely to appear in the file. We trust that.
	Buffer buffer;
	FILE *fp = File::OpenCFile(destPath, "wb");
	if (!fp) {
		ERROR_LOG(Log::HTTP, "Failed to open destination file '%s' for writing", destPath.ToVisualString().c_str());
		return;
	}
	u64 bytesWritten = 0;
	while (true) {
		// NOTE: Lines here can be extremely long, especially in compressed data. So we should split ReadLine up if needed.
		std::string line = request.In()->ReadLine();
		if (line == "--" + boundary || line == "--" + boundary + "--") {
			INFO_LOG(Log::HTTP, "Line matches boundary, breaking.");
			// Done.
			break;
		}
		// Write the line and a newline (since we ate it). TODO: This could be avoided.
		size_t len = line.size();
		if (fwrite(line.data(), 1, len, fp) != len || fwrite("\r\n", 1, 2, fp) != 2) {
			ERROR_LOG(Log::HTTP, "Failed to write %d bytes to destination file '%s' - bailing", (int)len, destPath.ToVisualString().c_str());
			fclose(fp);
			return;
		}
		bytesWritten += line.size();
	}

	INFO_LOG(Log::HTTP, "Total bytes written: %d", (int)bytesWritten);
	fclose(fp);

	// NOTE: We already read the boundary above.

	// Now the buffer should be empty.
	if (!request.In()->Empty()) {
		WARN_LOG(Log::HTTP, "We didn't drain the request.");
		std::string extraLine = request.In()->ReadLine();
		INFO_LOG(Log::HTTP, "Extra line: %s", extraLine.c_str());
	}

	INFO_LOG(Log::HTTP, "Upload of '%s' complete.", filename.c_str());
	// request.WriteHttpResponseHeader("1.0", 200, -1, "text/plain");

	const size_t blockSize = 16 * 1024;

}

void WebServerSetUploadPath(const Path &path) {
	std::lock_guard<std::mutex> guard(g_webServerLock);
	g_uploadPath = path;
}

static void WebServerThread() {
	SetCurrentThreadName("HTTPServer");

	AndroidJNIThreadContext context;  // Destructor detaches.

	auto http = new http::Server(new NewThreadExecutor());
	http->RegisterHandler("/", &HandleListing);
	// This lists all the (current) recent ISOs. It also handles the debugger, which is very ugly.
	http->SetFallbackHandler(&HandleFallback);
	http->RegisterHandler("/debugger", &ForwardDebuggerRequest);
	http->RegisterHandler("/upload_file", &HandleUploadPost);

	if (!http->Listen(g_Config.iRemoteISOPort, "debugger-webserver")) {
		if (!http->Listen(0, "debugger-webserver")) {
			ERROR_LOG(Log::FileSystem, "Unable to listen on any port (debugger - webserver)");
			UpdateStatus(ServerStatus::FINISHED);
			return;
		}
	}

	UpdateStatus(ServerStatus::RUNNING);

	g_Config.iRemoteISOPort = http->Port();
	RegisterServer(http->Port());
	double lastRegister = time_now_d();

	INFO_LOG(Log::HTTP, "Entering web server loop. Listening on port %d", g_Config.iRemoteISOPort);
	while (RetrieveStatus() == ServerStatus::RUNNING) {
		constexpr double webServerSliceSeconds = 0.2f;
		http->RunSlice(webServerSliceSeconds);
		double now = time_now_d();
		if (now > lastRegister + 540.0) {
			RegisterServer(http->Port());
			lastRegister = now;
		}
	}
	INFO_LOG(Log::HTTP, "Leaving web server loop.");

	http->Stop();
	StopAllDebuggers();
	delete http;

	UpdateStatus(ServerStatus::FINISHED);
}

// Only adds flags.
bool StartWebServer(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	switch (serverStatus) {
	case ServerStatus::RUNNING:
		if (((int)serverFlags & (int)flags) == (int)flags) {
			// Already running with these flags.
			return false;
		}
		serverFlags |= flags;
		return true;

	case ServerStatus::FINISHED:
		serverThread.join();
		[[fallthrough]]; // Intentional fallthrough.

	case ServerStatus::STOPPED:
		serverStatus = ServerStatus::STARTING;
		serverFlags = flags;
		serverThread = std::thread(&WebServerThread);
		return true;

	default:
		return false;
	}
}

// Only removes flags.
bool StopWebServer(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus != ServerStatus::RUNNING) {
		return false;
	}

	serverFlags &= ~flags;
	if (serverFlags == WebServerFlags::NONE) {
		serverStatus = ServerStatus::STOPPING;
	}
	return true;
}

bool WebServerStopping(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	return serverStatus == ServerStatus::STOPPING;
}

bool WebServerStopped(WebServerFlags flags) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	if (serverStatus == ServerStatus::RUNNING) {
		return !(serverFlags & flags);
	}
	return serverStatus == ServerStatus::STOPPED || serverStatus == ServerStatus::FINISHED;
}

void ShutdownWebServer() {
	StopWebServer(WebServerFlags::ALL);

	if (serverStatus != ServerStatus::STOPPED)
		serverThread.join();
	serverStatus = ServerStatus::STOPPED;
}

bool WebServerRunning(WebServerFlags flags) {
	return RetrieveStatus() == ServerStatus::RUNNING && (serverFlags & flags) != 0;
}

int WebServerPort() {
	return g_Config.iRemoteISOPort;
}
