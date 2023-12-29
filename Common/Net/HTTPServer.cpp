#include "Common/TimeUtil.h"
#include "ppsspp_config.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

#else

#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <sys/wait.h>         /*  for waitpid()             */
#include <netinet/in.h>       /*  struct sockaddr_in        */
#include <arpa/inet.h>        /*  inet (3) funtions         */
#include <unistd.h>           /*  misc. UNIX functions      */

#define closesocket close

#endif

#if PPSSPP_PLATFORM(UWP)
#define in6addr_any IN6ADDR_ANY_INIT
#endif

#include <algorithm>
#include <functional>

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "Common/Net/HTTPServer.h"
#include "Common/Net/NetBuffer.h"
#include "Common/Net/Sinks.h"
#include "Common/File/FileDescriptor.h"

#include "Common/Buffer.h"
#include "Common/Log.h"


void NewThreadExecutor::Run(std::function<void()> func) {
	threads_.push_back(std::thread(func));
}

NewThreadExecutor::~NewThreadExecutor() {
	// If Run was ever called...
	for (auto &thread : threads_)
		thread.join();
	threads_.clear();
}

namespace http {

// Note: charset here helps prevent XSS.
const char *const DEFAULT_MIME_TYPE = "text/html; charset=utf-8";

ServerRequest::ServerRequest(int fd)
	: fd_(fd) {
	in_ = new net::InputSink(fd);
	out_ = new net::OutputSink(fd);
	header_.ParseHeaders(in_);

	if (header_.ok) {
		VERBOSE_LOG(IO, "The request carried with it %i bytes", (int)header_.content_length);
	} else {
	    Close();
	}
}

ServerRequest::~ServerRequest() {
	Close();

	if (!in_->Empty()) {
		ERROR_LOG(IO, "Input not empty - invalid request?");
	}
	delete in_;
	if (!out_->Empty()) {
		WARN_LOG(IO, "Output not empty - connection abort? (%s) (%d bytes)", this->header_.resource, (int)out_->BytesRemaining());
	}
	delete out_;
}

void ServerRequest::WriteHttpResponseHeader(const char *ver, int status, int64_t size, const char *mimeType, const char *otherHeaders) const {
	const char *statusStr;
	switch (status) {
	case 200: statusStr = "OK"; break;
	case 206: statusStr = "Partial Content"; break;
	case 301: statusStr = "Moved Permanently"; break;
	case 302: statusStr = "Found"; break;
	case 304: statusStr = "Not Modified"; break;
	case 400: statusStr = "Bad Request"; break;
	case 403: statusStr = "Forbidden"; break;
	case 404: statusStr = "Not Found"; break;
	case 405: statusStr = "Method Not Allowed"; break;
	case 406: statusStr = "Not Acceptable"; break;
	case 410: statusStr = "Gone"; break;
	case 416: statusStr = "Range Not Satisfiable"; break;
	case 418: statusStr = "I'm a teapot"; break;
	case 500: statusStr = "Internal Server Error"; break;
	case 503: statusStr = "Service Unavailable"; break;
	default: statusStr = "OK"; break;
	}

	net::OutputSink *buffer = Out();
	buffer->Printf("HTTP/%s %03d %s\r\n", ver, status, statusStr);
	buffer->Push("Server: PPSSPPServer v0.1\r\n");
	if (!mimeType || strcmp(mimeType, "websocket") != 0) {
		buffer->Printf("Content-Type: %s\r\n", mimeType ? mimeType : DEFAULT_MIME_TYPE);
		buffer->Push("Connection: close\r\n");
	}
	if (size >= 0) {
		buffer->Printf("Content-Length: %llu\r\n", size);
	}
	if (otherHeaders) {
		buffer->Push(otherHeaders, strlen(otherHeaders));
	}
	buffer->Push("\r\n");
}

void ServerRequest::WritePartial() const {
	_assert_(fd_);
	out_->Flush();
}

void ServerRequest::Write() {
	_assert_(fd_);
	WritePartial();
	Close();
}

void ServerRequest::Close() {
	if (fd_) {
		closesocket(fd_);
		fd_ = 0;
	}
}

Server::Server(NewThreadExecutor *executor)
	: port_(0), executor_(executor) {
	RegisterHandler("/", std::bind(&Server::HandleListing, this, std::placeholders::_1));
	SetFallbackHandler(std::bind(&Server::Handle404, this, std::placeholders::_1));
}

Server::~Server() {
	delete executor_;
}

void Server::RegisterHandler(const char *url_path, UrlHandlerFunc handler) {
	handlers_[std::string(url_path)] = handler;
}

void Server::SetFallbackHandler(UrlHandlerFunc handler) {
	fallback_ = handler;
}

bool Server::Listen(int port, net::DNSType type) {
	bool success = false;
	if (type == net::DNSType::ANY || type == net::DNSType::IPV6) {
		success = Listen6(port, type == net::DNSType::IPV6);
	}
	if (!success && (type == net::DNSType::ANY || type == net::DNSType::IPV4)) {
		success = Listen4(port);
	}
	return success;
}

bool Server::Listen4(int port) {
	listener_ = socket(AF_INET, SOCK_STREAM, 0);
	if (listener_ < 0)
		return false;

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	int opt = 1;
	// Enable re-binding to avoid the pain when restarting the server quickly.
	setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

	if (bind(listener_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
#if PPSSPP_PLATFORM(WINDOWS)
		int err = WSAGetLastError();
#else
		int err = errno;
#endif
		ERROR_LOG(IO, "Failed to bind to port %d, error=%d - Bailing (ipv4)", port, err);
		closesocket(listener_);
		return false;
	}

	fd_util::SetNonBlocking(listener_, true);

	// 1024 is the max number of queued requests.
	if (listen(listener_, 1024) < 0) {
		closesocket(listener_);
		return false;
	}

	socklen_t len = sizeof(server_addr);
	if (getsockname(listener_, (struct sockaddr *)&server_addr, &len) == 0) {
		port = ntohs(server_addr.sin_port);
	}

	INFO_LOG(IO, "HTTP server started on port %d", port);
	port_ = port;

	return true;
}

bool Server::Listen6(int port, bool ipv6_only) {
#if !PPSSPP_PLATFORM(SWITCH)
	listener_ = socket(AF_INET6, SOCK_STREAM, 0);
	if (listener_ < 0)
		return false;

	struct sockaddr_in6 server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_addr = in6addr_any;
	server_addr.sin6_port = htons(port);

	int opt = 1;
	// Enable re-binding to avoid the pain when restarting the server quickly.
	setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

	// Enable listening on IPv6 and IPv4?
	opt = ipv6_only ? 1 : 0;
	setsockopt(listener_, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&opt, sizeof(opt));

	if (bind(listener_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
#if PPSSPP_PLATFORM(WINDOWS)
		int err = WSAGetLastError();
#else
		int err = errno;
#endif
		ERROR_LOG(IO, "Failed to bind to port %d, error=%d - Bailing (ipv6)", port, err);
		closesocket(listener_);
		return false;
	}

	fd_util::SetNonBlocking(listener_, true);

	// 1024 is the max number of queued requests.
	if (listen(listener_, 1024) < 0) {
		closesocket(listener_);
		return false;
	}

	socklen_t len = sizeof(server_addr);
	if (getsockname(listener_, (struct sockaddr *)&server_addr, &len) == 0) {
		port = ntohs(server_addr.sin6_port);
	}

	INFO_LOG(IO, "HTTP server started on port %d", port);
	port_ = port;

	return true;
#else
	return false;
#endif
}

bool Server::RunSlice(double timeout) {
	if (listener_ < 0 || port_ == 0) {
		return false;
	}

	if (timeout <= 0.0) {
		timeout = 86400.0;
	}
	if (!fd_util::WaitUntilReady(listener_, timeout, false)) {
		return false;
	}

	union {
		struct sockaddr sa;
		struct sockaddr_in ipv4;
#if !PPSSPP_PLATFORM(SWITCH)
		struct sockaddr_in6 ipv6;
#endif
	} client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	int conn_fd = accept(listener_, &client_addr.sa, &client_addr_size);
	if (conn_fd >= 0) {
		executor_->Run(std::bind(&Server::HandleConnection, this, conn_fd));
		return true;
	}
	else {
		ERROR_LOG(IO, "socket accept failed: %i", conn_fd);
		return false;
	}
}

bool Server::Run(int port) {
	if (!Listen(port)) {
		return false;
	}

	while (true) {
		RunSlice(0.0);
	}

	// We'll never get here. Ever.
	return true;
}

void Server::Stop() {
	closesocket(listener_);
}

void Server::HandleConnection(int conn_fd) {
	ServerRequest request(conn_fd);
	if (!request.IsOK()) {
		WARN_LOG(IO, "Bad request, ignoring.");
		return;
	}
	HandleRequest(request);

	// TODO: Way to mark the content body as read, read it here if never read.
	// This allows the handler to stream if need be.

	// TODO: Could handle keep alive here.
	request.Write();
}

void Server::HandleRequest(const ServerRequest &request) {
	HandleRequestDefault(request);
}

void Server::HandleRequestDefault(const ServerRequest &request) {
	if (request.resource() == nullptr) {
		fallback_(request);
		return;
	}
	// First, look through all handlers. If we got one, use it.
	auto handler = handlers_.find(request.resource());
	if (handler != handlers_.end()) {
		(handler->second)(request);
	} else {
		// Let's hit the 404 handler instead.
		fallback_(request);
	}
}

void Server::Handle404(const ServerRequest &request) {
	INFO_LOG(IO, "No handler for '%s', falling back to 404.", request.resource());
	const char *payload = "<html><body>404 not found</body></html>\r\n";
	request.WriteHttpResponseHeader("1.0", 404, (int)strlen(payload));
	request.Out()->Push(payload);
}

void Server::HandleListing(const ServerRequest &request) {
	request.WriteHttpResponseHeader("1.0", 200, -1, "text/plain");
	for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
		request.Out()->Printf("%s\n", iter->first.c_str());
	}
}

}  // namespace http
