#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <cstdint>
#include <string>

#include "Common/File/Path.h"
#include "Common/Net/NetBuffer.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/Connection.h"

namespace http {

bool GetHeaderValue(const std::vector<std::string> &responseHeaders, std::string_view header, std::string *value);

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

}  // namespace http
