#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <cstdint>

#include "Common/File/Path.h"
#include "Common/Net/NetBuffer.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/HTTPRequest.h"

namespace net {

class Connection {
public:
	Connection();
	virtual ~Connection();

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

};

}	// namespace net

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
	Client();
	~Client();

	// Return value is the HTTP return code. 200 means OK. < 0 means some local error.
	int GET(const RequestParams &req, Buffer *output, net::RequestProgress *progress);
	int GET(const RequestParams &req, Buffer *output, std::vector<std::string> &responseHeaders, net::RequestProgress *progress);

	// Return value is the HTTP return code.
	int POST(const RequestParams &req, const std::string &data, const std::string &mime, Buffer *output, net::RequestProgress *progress);
	int POST(const RequestParams &req, const std::string &data, Buffer *output, net::RequestProgress *progress);

	// HEAD, PUT, DELETE aren't implemented yet, but can be done with SendRequest.

	int SendRequest(const char *method, const RequestParams &req, const char *otherHeaders, net::RequestProgress *progress);
	int SendRequestWithData(const char *method, const RequestParams &req, const std::string &data, const char *otherHeaders, net::RequestProgress *progress);
	int ReadResponseHeaders(net::Buffer *readbuf, std::vector<std::string> &responseHeaders, net::RequestProgress *progress);
	// If your response contains a response, you must read it.
	int ReadResponseEntity(net::Buffer *readbuf, const std::vector<std::string> &responseHeaders, Buffer *output, net::RequestProgress *progress);

	void SetDataTimeout(double t) {
		dataTimeout_ = t;
	}

	void SetUserAgent(const std::string &value) {
		userAgent_ = value;
	}

protected:
	std::string userAgent_;
	double dataTimeout_ = 900.0;
};

// Really an asynchronous request.
class HTTPRequest : public Request {
public:
	HTTPRequest(RequestMethod method, const std::string &url, const std::string &postData, const std::string &postMime, const Path &outfile, ProgressBarMode progressBarMode = ProgressBarMode::DELAYED, std::string_view name = "");
	~HTTPRequest();

	void Start() override;
	void Join() override;

	bool Done() override { return completed_; }
	bool Failed() const override { return failed_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const override { return resultCode_; }

	const Path &outfile() const override { return outfile_; }

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() override { return buffer_; }
	const Buffer &buffer() const override { return buffer_; }

	void Cancel() override {
		cancelled_ = true;
	}

	bool IsCancelled() const override {
		return cancelled_;
	}

private:
	void Do();  // Actually does the download. Runs on thread.
	int Perform(const std::string &url);
	std::string RedirectLocation(const std::string &baseUrl);
	void SetFailed(int code);

	std::string postData_;
	Buffer buffer_;
	std::vector<std::string> responseHeaders_;
	Path outfile_;
	std::thread thread_;
	std::string postMime_;
	int resultCode_ = 0;
	bool completed_ = false;
	bool failed_ = false;
	bool cancelled_ = false;
	bool joined_ = false;
};

}	// http
