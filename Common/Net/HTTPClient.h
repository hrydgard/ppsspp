#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <cstdint>

#include "Common/File/Path.h"
#include "Common/Net/NetBuffer.h"
#include "Common/Net/Resolve.h"

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

struct RequestProgress {
	RequestProgress() {}
	explicit RequestProgress(bool *c) : cancelled(c) {}

	float progress = 0.0f;
	float kBps = 0.0f;
	bool *cancelled = nullptr;
};

struct RequestParams {
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
	int GET(const RequestParams &req, Buffer *output, RequestProgress *progress);
	int GET(const RequestParams &req, Buffer *output, std::vector<std::string> &responseHeaders, RequestProgress *progress);

	// Return value is the HTTP return code.
	int POST(const RequestParams &req, const std::string &data, const std::string &mime, Buffer *output, RequestProgress *progress);
	int POST(const RequestParams &req, const std::string &data, Buffer *output, RequestProgress *progress);

	// HEAD, PUT, DELETE aren't implemented yet, but can be done with SendRequest.

	int SendRequest(const char *method, const RequestParams &req, const char *otherHeaders, RequestProgress *progress);
	int SendRequestWithData(const char *method, const RequestParams &req, const std::string &data, const char *otherHeaders, RequestProgress *progress);
	int ReadResponseHeaders(net::Buffer *readbuf, std::vector<std::string> &responseHeaders, RequestProgress *progress);
	// If your response contains a response, you must read it.
	int ReadResponseEntity(net::Buffer *readbuf, const std::vector<std::string> &responseHeaders, Buffer *output, RequestProgress *progress);

	void SetDataTimeout(double t) {
		dataTimeout_ = t;
	}

	void SetUserAgent(const std::string &&value) {
		userAgent_ = value;
	}

protected:
	std::string userAgent_;
	const char *httpVersion_;
	double dataTimeout_ = 900.0;
};

// Not particularly efficient, but hey - it's a background download, that's pretty cool :P
class Download {
public:
	Download(const std::string &url, const Path &outfile);
	~Download();

	void Start();

	void Join();

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_.progress; }
	float SpeedKBps() const { return progress_.kBps; }

	bool Done() const { return completed_; }
	bool Failed() const { return failed_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const { return resultCode_; }

	std::string url() const { return url_; }
	const Path &outfile() const { return outfile_; }

	void SetAccept(const char *mime) {
		acceptMime_ = mime;
	}

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() { return buffer_; }
	const Buffer &buffer() const { return buffer_; }

	void Cancel() {
		cancelled_ = true;
	}

	bool IsCancelled() const {
		return cancelled_;
	}

	// NOTE: Callbacks are NOT executed until RunCallback is called. This is so that
	// the call will end up on the thread that calls g_DownloadManager.Update().
	void SetCallback(std::function<void(Download &)> callback) {
		callback_ = callback;
	}
	void RunCallback() {
		if (callback_) {
			callback_(*this);
		}
	}

	// Just metadata. Convenient for download managers, for example, if set,
	// Downloader::GetCurrentProgress won't return it in the results.
	bool IsHidden() const { return hidden_; }
	void SetHidden(bool hidden) { hidden_ = hidden; }

private:
	void Do();  // Actually does the download. Runs on thread.
	int PerformGET(const std::string &url);
	std::string RedirectLocation(const std::string &baseUrl);
	void SetFailed(int code);

	RequestProgress progress_;
	Buffer buffer_;
	std::vector<std::string> responseHeaders_;
	std::string url_;
	Path outfile_;
	std::thread thread_;
	const char *acceptMime_ = "*/*";
	int resultCode_ = 0;
	bool completed_ = false;
	bool failed_ = false;
	bool cancelled_ = false;
	bool hidden_ = false;
	bool joined_ = false;
	std::function<void(Download &)> callback_;
};

using std::shared_ptr;

class Downloader {
public:
	~Downloader() {
		CancelAll();
	}

	std::shared_ptr<Download> StartDownload(const std::string &url, const Path &outfile, const char *acceptMime = nullptr);

	std::shared_ptr<Download> StartDownloadWithCallback(
		const std::string &url,
		const Path &outfile,
		std::function<void(Download &)> callback,
		const char *acceptMime = nullptr);

	// Drops finished downloads from the list.
	void Update();
	void CancelAll();

	std::vector<float> GetCurrentProgress();

private:
	std::vector<std::shared_ptr<Download>> downloads_;
};

}	// http
