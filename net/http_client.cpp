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
#include "data/compression.h"
#include "net/resolve.h"
#include "net/url.h"

// #include "strings/strutil.h"

namespace net {

Connection::Connection() 
		: port_(-1), sock_(-1), resolved_(NULL) {
}

Connection::~Connection() {
	Disconnect();
	if (resolved_ != NULL)
		DNSResolveFree(resolved_);
}

// For whatever crazy reason, htons isn't available on android x86 on the build server. so here we go.

// TODO: Fix for big-endian
inline unsigned short myhtons(unsigned short x) {
	return (x >> 8) | (x << 8);
}

bool Connection::Resolve(const char *host, int port) {
	if ((intptr_t)sock_ != -1) {
		ELOG("Resolve: Already have a socket");
		return false;
	}

	host_ = host;
	port_ = port;

	char port_str[10];
	snprintf(port_str, sizeof(port_str), "%d", port);
	
	std::string err;
	if (!net::DNSResolve(host, port_str, &resolved_, err)) {
		ELOG("Failed to resolve host %s: %s", host, err.c_str());
		// So that future calls fail.
		port_ = 0;
		return false;
	}

	return true;
}

bool Connection::Connect() {
	CHECK_GE(port_, 0);
	sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ((intptr_t)sock_ < 0) {
		ELOG("Bad socket");
		return false;
	}

	for (int tries = 100; tries > 0; --tries) {
		for (addrinfo *possible = resolved_; possible != NULL; possible = possible->ai_next) {
			// TODO: Could support ipv6 without huge difficulty...
			if (possible->ai_family != AF_INET)
				continue;

			int retval = connect(sock_, possible->ai_addr, (int)possible->ai_addrlen);
			if (retval >= 0)
				return true;
		}
#ifdef _WIN32
		Sleep(1);
#else
		sleep(1);
#endif
	}
	return false;
}

void Connection::Disconnect() {
	if ((intptr_t)sock_ != -1) {
		closesocket(sock_);
		sock_ = -1;
	}
}

}	// net

namespace http {

Client::Client() {

}

Client::~Client() {
	Disconnect();
}

// TODO: do something sane here
#define USERAGENT "NATIVEAPP 1.0"


void DeChunk(Buffer *inbuffer, Buffer *outbuffer) {
	while (true) {
		std::string line;
		inbuffer->TakeLineCRLF(&line);
		if (!line.size())
			return;
		int chunkSize;
		sscanf(line.c_str(), "%x", &chunkSize);
		if (chunkSize) {
			std::string data;
			inbuffer->Take(chunkSize, &data);
			outbuffer->Append(data);
		} else {
			// a zero size chunk should mean the end.
			inbuffer->clear();
			return;
		}
		inbuffer->Skip(2);
	}
}

int Client::GET(const char *resource, Buffer *output) {
	Buffer buffer;
	const char *tpl =
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: " USERAGENT "\r\n"
		"Accept: */*\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"\r\n";

	buffer.Printf(tpl, resource, host_.c_str());
	bool flushed = buffer.FlushSocket(sock());
	if (!flushed) {
		return -1;  // TODO error code.
	}

	Buffer readbuf;

	// Snarf all the data we can into RAM. A little unsafe but hey.
	if (!readbuf.ReadAll(sock()))
		return -1;

	// Grab the first header line that contains the http code.

	// Skip the header. TODO: read HTTP code and file size so we can make progress bars.

	std::string line;
	readbuf.TakeLineCRLF(&line);
	int code = atoi(&line[line.find(" ") + 1]);

	bool gzip = false;
	int contentLength = 0;
	while (true) {
		int sz = readbuf.TakeLineCRLF(&line);
		if (!sz)
			break;
		if (startsWith(line, "Content-Length:")) {
			contentLength = atoi(&line[16]);
		} else if (startsWith(line, "Content-Encoding:")) {
			if (line.find("gzip") != std::string::npos) {
				gzip = true;
			}
		}
	}

	// output now contains the rest of the reply. Dechunk it.
	DeChunk(&readbuf, output);

	// If it's gzipped, we decompress it and put it back in the buffer.
	if (gzip) {
		std::string compressed;
		output->TakeAll(&compressed);
		// What is this garbage?
		//if (compressed[0] == 0x8e)
//			compressed = compressed.substr(4);
		std::string decompressed;
		bool result = decompress_string(compressed, &decompressed);
		if (!result) {
			ELOG("Error decompressing using zlib");
			return -1;
		}
		output->Append(decompressed);
	}

	return code;
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
	if (!buffer.FlushSocket(sock())) {
		ELOG("Failed posting");
	}

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
	int code = atoi(&firstline[9]);
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

Download::Download(const std::string &url, const std::string &outfile)
	: url_(url), outfile_(outfile), progress_(0.0f), failed_(false), resultCode_(0) {
	std::thread th(std::bind(&Download::Do, this));
	th.detach();
}

Download::~Download() {

}

void Download::Do() {
	resultCode_ = 0;

	Url fileUrl(url_);
	if (!fileUrl.Valid()) {
		failed_ = true;
		progress_ = 1.0f;
		return;
	}
	net::Init();

	http::Client client;
	if (!client.Resolve(fileUrl.Host().c_str(), 80)) {
		ELOG("Failed resolving %s", url_.c_str());
		failed_ = true;
		progress_ = 1.0f;
		net::Shutdown();
		return;
	}
	if (!client.Connect()) {
		ELOG("Failed connecting to server.");
		resultCode_ = -1;
		net::Shutdown();
		progress_ = 1.0f;
		return;
	}
	int resultCode = client.GET(fileUrl.Resource().c_str(), &buffer_);
	if (resultCode == 200) {
		ILOG("Completed downloading %s to %s", url_.c_str(), outfile_.c_str());
		if (!outfile_.empty() && !buffer_.FlushToFile(outfile_.c_str())) {
			ELOG("Failed writing download to %s", outfile_.c_str());
		}
	} else {
		ELOG("Error downloading %s to %s: %i", url_.c_str(), outfile_.c_str(), resultCode);
	}
	resultCode_ = resultCode;
	net::Shutdown();
	progress_ = 1.0f;
}

std::shared_ptr<Download> Downloader::StartDownload(const std::string &url, const std::string &outfile) {
	std::shared_ptr<Download> dl(new Download(url, outfile));
	downloads_.push_back(dl);
	return dl;
}

void Downloader::Update() {
	restart:
	for (size_t i = 0; i < downloads_.size(); i++) {
		if (downloads_[i]->Progress() == 1.0f || downloads_[i]->Failed()) {
			downloads_.erase(downloads_.begin() + i);
			goto restart;
		}
	}
}

}	// http
