#include "net/http_client.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/buffer.h"
#include "base/stringutil.h"
#include "net/resolve.h"
// #include "strings/strutil.h"

namespace net {

Connection::Connection() 
		: port_(-1), sock_(-1) {
}

Connection::~Connection() {
	Disconnect();
}

// For whatever crazy reason, htons isn't available on android x86 on the build server. so here we go.

// TODO: Fix for big-endian
inline unsigned short myhtons(unsigned short x) {
	return (x >> 8) | (x << 8);
}

bool Connection::Resolve(const char *host, int port) {
	CHECK_EQ(-1, (intptr_t)sock_);
	host_ = host;
	port_ = port;

	const char *err;
	const char *ip = net::DNSResolveTry(host, &err);
	if (ip == NULL) {
		ELOG("Failed to resolve host %s", host);
		// So that future calls fail.
		port_ = 0;
		return false;
	}
	// VLOG(1) << "Resolved " << host << " to " << ip;
	remote_.sin_family = AF_INET;
	int tmpres = net::inet_pton(AF_INET, ip, (void *)(&(remote_.sin_addr.s_addr)));
	CHECK_GE(tmpres, 0);	// << "inet_pton failed";
	CHECK_NE(0, tmpres);	// << ip << " not a valid IP address";
	remote_.sin_port = myhtons(port);
	free((void *)ip);
	return true;
}

void Connection::Connect() {
	CHECK_GE(port_, 0);
	sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	CHECK_GE(sock_, 0);

	// poll once per second.. should find a way to do this blocking.
	int retval = -1;
	while (retval < 0) {
		retval = connect(sock_, (sockaddr *)&remote_, sizeof(struct sockaddr));
		if (retval >= 0) break;
#ifdef _WIN32
		Sleep(1);
#else
		sleep(1);
#endif
	}
}

void Connection::Disconnect() {
	if ((intptr_t)sock_ != -1) {
		closesocket(sock_);
		sock_ = -1;
	} else {
		WLOG("Socket was already disconnected.");
	}
}

void Connection::Reconnect() {
	Disconnect();
	Connect();
}

}	// net

namespace http {

Client::Client() {
}
Client::~Client() {
}

#define USERAGENT "METAGET 1.0"

void Client::GET(const char *resource, Buffer *output) {
	Buffer buffer;
	const char *tpl = "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: " USERAGENT "\r\n\r\n";
	buffer.Printf(tpl, resource, host_.c_str());
	CHECK(buffer.FlushSocket(sock()));

	// Snarf all the data we can.
	output->ReadAll(sock());

	// Skip the header.
	while (output->SkipLineCRLF() > 0)
		;

	// output now contains the rest of the reply.
}

int Client::POST(const char *resource, const std::string &data, const std::string &mime, Buffer *output) {
	Buffer buffer;
	const char *tpl = "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: " USERAGENT "\r\nContent-Length: %d\r\n";
	buffer.Printf(tpl, resource, host_.c_str(), (int)data.size());
	if (!mime.empty()) {
		buffer.Printf("Content-Type: %s\r\n", mime.c_str());
	}
	buffer.Append("\r\n");
	buffer.Append(data);
	CHECK(buffer.FlushSocket(sock()));

	// I guess we could add a deadline here.
	output->ReadAll(sock());

	if (output->size() == 0) {
		// The connection was closed.
		ELOG("POST failed.");
		return -1;
	}

	std::string debug_data;
	output->PeekAll(&debug_data);

	//VLOG(1) << "Reply size (before stripping headers): " << debug_data.size();
	std::string debug_str;
	StringToHexString(debug_data, &debug_str);
	// Tear off the http headers, leaving the actual response data.
	std::string firstline;
	CHECK_GT(output->TakeLineCRLF(&firstline), 0);
	int code = atoi(&firstline[9]);	// ugggly hardcoding
	//VLOG(1) << "HTTP result code: " << code;
	while (true) {
		int skipped = output->SkipLineCRLF();
		if (skipped == 0)
			break;
	}
	output->PeekAll(&debug_data);
	return code;
}

int Client::POST(const char *resource, const std::string &data, Buffer *output) {
	return POST(resource, data, "", output);
}

}	// http
