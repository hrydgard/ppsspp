#ifndef _NET_HTTP_HTTP_CLIENT
#define _NET_HTTP_HTTP_CLIENT

#include <memory>
#include "base/basictypes.h"
#include "base/buffer.h"
#include "thread/thread.h"
#include "base/functional.h"

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
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

	bool Connect(int maxTries = 2, double timeout = 20.0f);
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
	int GET(const char *resource, Buffer *output, float *progress = nullptr);

	// Return value is the HTTP return code.
	int POST(const char *resource, const std::string &data, const std::string &mime, Buffer *output, float *progress = nullptr);
	int POST(const char *resource, const std::string &data, Buffer *output, float *progress = nullptr);

	// HEAD, PUT, DELETE aren't implemented yet, but can be done with SendRequest.

	int SendRequest(const char *method, const char *resource, const char *otherHeaders = nullptr, float *progress = nullptr);
	int SendRequestWithData(const char *method, const char *resource, const std::string &data, const char *otherHeaders = nullptr, float *progress = nullptr);
	int ReadResponseHeaders(Buffer *readbuf, std::vector<std::string> &responseHeaders, float *progress = nullptr);
	// If your response contains a response, you must read it.
	int ReadResponseEntity(Buffer *readbuf, const std::vector<std::string> &responseHeaders, Buffer *output, float *progress = nullptr);

	const char *userAgent_;
	const char *httpVersion_;
};

// Not particularly efficient, but hey - it's a background download, that's pretty cool :P
class Download {
public:
	Download(const std::string &url, const std::string &outfile);
	~Download();

	// Keeps around an instance of the shared_ptr, so that it doesn't get destructed
	// until done.
	void Start(std::shared_ptr<Download> self);

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_; }

	bool Done() const { return completed_; }
	bool Failed() const { return failed_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const { return resultCode_; }

	std::string url() const { return url_; }
	std::string outfile() const { return outfile_; }

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
	void Do(std::shared_ptr<Download> self);  // Actually does the download. Runs on thread.
	void SetFailed(int code);
	float progress_;
	Buffer buffer_;
	std::string url_;
	std::string outfile_;
	int resultCode_;
	bool completed_;
	bool failed_;
	volatile bool cancelled_;
	bool hidden_;
	std::function<void(Download &)> callback_;
};

using std::shared_ptr;

class Downloader {
public:
	~Downloader() {
		CancelAll();
	}

	std::shared_ptr<Download> StartDownload(const std::string &url, const std::string &outfile);

	std::shared_ptr<Download> StartDownloadWithCallback(
		const std::string &url,
		const std::string &outfile,
		std::function<void(Download &)> callback);

	// Drops finished downloads from the list.
	void Update();
	void CancelAll();

	std::vector<float> GetCurrentProgress();

private:
	std::vector<std::shared_ptr<Download>> downloads_;
};


}	// http

#endif	// _NET_HTTP_HTTP_CLIENT

