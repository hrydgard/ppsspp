#ifndef _NET_HTTP_HTTP_CLIENT
#define _NET_HTTP_HTTP_CLIENT

#include <memory>
#include "base/basictypes.h"
#include "base/buffer.h"
#include "thread/thread.h"
#include "base/functional.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#if defined(__FreeBSD__) || defined(__SYMBIAN32__)
#include <netinet/in.h>
#else
#include <arpa/inet.h>
#endif
#include <sys/socket.h>
#include <netdb.h>
#endif

namespace net {

class Connection {
public:
	Connection();
	virtual ~Connection();

	// Inits the sockaddr_in.
	bool Resolve(const char *host, int port);

	bool Connect();
	void Disconnect();

	// Only to be used for bring-up and debugging.
	uintptr_t sock() const { return sock_; }

protected:
	// Store the remote host here, so we can send it along through HTTP/1.1 requests.
	// TODO: Move to http::client?
	std::string host_;
	int port_;

	addrinfo *resolved_;

private:
	uintptr_t sock_;

};

}	// namespace net

namespace http {

class Client : public net::Connection {
public:
	Client();
	~Client();

	// Return value is the HTTP return code. 200 means OK. < 0 means some local error.
	int GET(const char *resource, Buffer *output);

	// Return value is the HTTP return code.
	int POST(const char *resource, const std::string &data, const std::string &mime, Buffer *output);
	int POST(const char *resource, const std::string &data, Buffer *output);

	// HEAD, PUT, DELETE aren't implemented yet.
};

// Not particularly efficient, but hey - it's a background download, that's pretty cool :P
class Download {
public:
	Download(const std::string &url, const std::string &outfile);
	~Download();

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_; }
	bool Done() const { return progress_ == 1.0f; }

	bool Failed() const { return failed_; }

	int ResultCode() const { return resultCode_; }

	std::string url() const { return url_; }
	std::string outfile() const { return outfile_; }

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() { return buffer_; }

private:
	void Do();  // Actually does the download. Runs on thread.

	float progress_;
	Buffer buffer_;
	std::string url_;
	std::string outfile_;
	int resultCode_;
	bool failed_;
};

using std::shared_ptr;

class Downloader {
public:
	std::shared_ptr<Download> StartDownload(const std::string &url, const std::string &outfile);

	// Drops finished downloads from the list.
	void Update();

private:
	std::vector<std::shared_ptr<Download>> downloads_;
};


}	// http

#endif	// _NET_HTTP_HTTP_CLIENT

