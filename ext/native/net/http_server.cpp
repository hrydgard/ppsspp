#include "base/timeutil.h"

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

#include <algorithm>
#include <functional>

#include <stdio.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/buffer.h"
#include "file/fd_util.h"
#include "net/http_server.h"
#include "net/sinks.h"
#include "thread/executor.h"

namespace http {

// Note: charset here helps prevent XSS.
const char *const DEFAULT_MIME_TYPE = "text/html; charset=utf-8";

Request::Request(int fd)
    : fd_(fd) {
	in_ = new net::InputSink(fd);
	out_ = new net::OutputSink(fd);
	header_.ParseHeaders(in_);

	if (header_.ok) {
		ILOG("The request carried with it %i bytes", (int)header_.content_length);
	} else {
	    Close();
	}
}

Request::~Request() {
	Close();

	CHECK(in_->Empty());
	delete in_;
	CHECK(out_->Empty());
	delete out_;
}

void Request::WriteHttpResponseHeader(int status, int64_t size, const char *mimeType, const char *otherHeaders) const {
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
	buffer->Printf("HTTP/1.0 %03d %s\r\n", status, statusStr);
	buffer->Push("Server: PPSSPPServer v0.1\r\n");
	buffer->Printf("Content-Type: %s\r\n", mimeType ? mimeType : DEFAULT_MIME_TYPE);
	buffer->Push("Connection: close\r\n");
	if (size >= 0) {
		buffer->Printf("Content-Length: %llu\r\n", size);
	}
	if (otherHeaders) {
		buffer->Push(otherHeaders, (int)strlen(otherHeaders));
	}
	buffer->Push("\r\n");
}

void Request::WritePartial() const {
  CHECK(fd_);
  out_->Flush();
}

void Request::Write() {
  CHECK(fd_);
  WritePartial();
  Close();
}

void Request::Close() {
  if (fd_) {
    closesocket(fd_);
    fd_ = 0;
  }
}

Server::Server(threading::Executor *executor)
  : port_(0), executor_(executor) {
  RegisterHandler("/", std::bind(&Server::HandleListing, this, std::placeholders::_1));
  SetFallbackHandler(std::bind(&Server::Handle404, this, std::placeholders::_1));
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
		closesocket(listener_);
		ELOG("Failed to bind to port %i. Bailing.", port);
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

	ILOG("HTTP server started on port %i", port);
	port_ = port;

	return true;
}

bool Server::Listen6(int port, bool ipv6_only) {
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
		closesocket(listener_);
		ELOG("Failed to bind to port %i. Bailing.", port);
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

	ILOG("HTTP server started on port %i", port);
	port_ = port;

	return true;
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
		struct sockaddr_in6 ipv6;
	} client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	int conn_fd = accept(listener_, &client_addr.sa, &client_addr_size);
	if (conn_fd >= 0) {
		executor_->Run(std::bind(&Server::HandleConnection, this, conn_fd));
		return true;
	}
	else {
		ELOG("socket accept failed: %i", conn_fd);
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
  Request request(conn_fd);
  if (!request.IsOK()) {
    WLOG("Bad request, ignoring.");
    return;
  }
  HandleRequestDefault(request);

  // TODO: Way to mark the content body as read, read it here if never read.
  // This allows the handler to stream if need be.

  // TODO: Could handle keep alive here.
  request.Write();
}

void Server::HandleRequest(const Request &request) {
	HandleRequestDefault(request);
}

void Server::HandleRequestDefault(const Request &request) {
	// First, look through all handlers. If we got one, use it.
	auto handler = handlers_.find(request.resource());
	if (handler != handlers_.end()) {
		(handler->second)(request);
	} else {
		// Let's hit the 404 handler instead.
		fallback_(request);
	}
}

void Server::Handle404(const Request &request) {
	ILOG("No handler for '%s', falling back to 404.", request.resource());
	const char *payload = "<html><body>404 not found</body></html>\r\n";
	request.WriteHttpResponseHeader(404, (int)strlen(payload));
	request.Out()->Push(payload);
}

void Server::HandleListing(const Request &request) {
	request.WriteHttpResponseHeader(200, -1, "text/plain");
	for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
		request.Out()->Printf("%s\n", iter->first.c_str());
	}
}

}  // namespace http
