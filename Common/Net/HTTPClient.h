#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <cstdint>
#include <string>

#include "Common/File/Path.h"
#include "Common/Net/NetBuffer.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/HTTPRequest.h"

namespace net {

typedef std::function<std::string(const std::string &)> ResolveFunc;

class Connection {
public:
	virtual ~Connection();

	explicit Connection(ResolveFunc func) : customResolve_(func) {}

	// Inits the sockaddr_in.
	bool Resolve(const char *host, int port, DNSType type = DNSType::ANY);

	bool Connect(int maxTries = 2, double timeout = 20.0f, bool *cancelConnect = nullptr);
	void Disconnect();

	// Only to be used for bring-up and debugging.
	uintptr_t sock() const { return sock_; }

protected:
	// Store the remote host here, so we can send it along through HTTP/1.1 requests.
	// TODO: Move to http::client?
	std::string host_;
	int port_ = -1;

	addrinfo *resolved_ = nullptr;

private:
	uintptr_t sock_ = -1;
	ResolveFunc customResolve_;
};

}  // namespace net

namespace http {

bool GetHeaderValue(const std::vector<std::string> &responseHeaders, const std::string &header, std::string *value);

class RequestParams {
public:
	RequestParams() {}
	explicit RequestParams(const char *r) : resource(r) {}
	RequestParams(const std::string &r, const char *a) : resource(r), acceptMime(a) {}

	std::string resource;
	const char *acceptMime = "*/*";
};

class Client : public net::Connection {
public:
	Client(net::ResolveFunc func);
	~Client();

	// Return value is the HTTP return code. 200 means OK. < 0 means some local error.
	int GET(const RequestParams &req, Buffer *output, net::RequestProgress *progress);
	int GET(const RequestParams &req, Buffer *output, std::vector<std::string> &responseHeaders, net::RequestProgress *progress);

	// Return value is the HTTP return code.
	int POST(const RequestParams &req, std::string_view data, std::string_view mime, Buffer *output, net::RequestProgress *progress);
	int POST(const RequestParams &req, std::string_view data, Buffer *output, net::RequestProgress *progress);

	// HEAD, PUT, DELETE aren't implemented yet, but can be done with SendRequest.

	int SendRequest(const char *method, const RequestParams &req, const char *otherHeaders, net::RequestProgress *progress);
	int SendRequestWithData(const char *method, const RequestParams &req, std::string_view data, const char *otherHeaders, net::RequestProgress *progress);
	int ReadResponseHeaders(net::Buffer *readbuf, std::vector<std::string> &responseHeaders, net::RequestProgress *progress, std::string *statusLine = nullptr);
	// If your response contains a response, you must read it.
	int ReadResponseEntity(net::Buffer *readbuf, const std::vector<std::string> &responseHeaders, Buffer *output, net::RequestProgress *progress);

	void SetDataTimeout(double t) {
		dataTimeout_ = t;
	}

	void SetUserAgent(std::string_view value) {
		userAgent_ = value;
	}

	void SetHttpVersion(const char *version) {
		httpVersion_ = version;
	}

protected:
	std::string userAgent_;
	const char* httpVersion_;
	double dataTimeout_ = 900.0;
};

// Really an asynchronous request.
class HTTPRequest : public Request {
public:
	HTTPRequest(RequestMethod method, std::string_view url, std::string_view postData, std::string_view postMime, const Path &outfile, RequestFlags flags, net::ResolveFunc customResolve, std::string_view name = "");
	~HTTPRequest();

	void Start() override;
	void Join() override;

	bool Done() override { return completed_; }
	bool Failed() const override { return failed_; }

private:
	void Do();  // Actually does the download. Runs on thread.
	int Perform(const std::string &url);
	std::string RedirectLocation(const std::string &baseUrl) const;
	void SetFailed(int code);

	std::string postData_;
	std::thread thread_;
	std::string postMime_;
	bool completed_ = false;
	bool failed_ = false;
	net::ResolveFunc customResolve_;
};

// Fake request for cache hits.
// The download manager uses this when caching was requested, and a new-enough file was present in the cache directory.
// This is simply a finished request, that can still be queried like a normal one so users don't know it came from the cache.
class CachedRequest : public Request {
public:
	CachedRequest(RequestMethod method, std::string_view url, std::string_view name, bool *cancelled, RequestFlags flags, std::string_view responseData)
		: Request(method, url, name, cancelled, flags)
	{
		buffer_.Append(responseData);
	}
	void Start() override {}
	void Join() override {}
	bool Done() override { return true; }
	bool Failed() const override { return false; }
};

}  // namespace http
