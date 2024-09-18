#pragma once

#include <string>
#include <functional>
#include <memory>

#include "Common/File/Path.h"
#include "Common/Net/NetBuffer.h"

namespace http {

enum class RequestMethod {
	GET,
	POST,
};

enum class ProgressBarMode {
	NONE,
	VISIBLE,
	DELAYED,
};

// Abstract request.
class Request {
public:
	Request(RequestMethod method, const std::string &url, std::string_view name, bool *cancelled, ProgressBarMode mode);
	virtual ~Request() {}

	void SetAccept(const char *mime) {
		acceptMime_ = mime;
	}

	void SetUserAgent(const std::string &userAgent) {
		userAgent_ = userAgent;
	}

	// NOTE: Completion callbacks (which these are) are deferred until RunCallback is called. This is so that
	// the call will end up on the thread that calls g_DownloadManager.Update().
	void SetCallback(std::function<void(Request &)> callback) {
		callback_ = callback;
	}
	void RunCallback() {
		if (callback_) {
			callback_(*this);
		}
	}

	virtual void Start() = 0;
	virtual void Join() = 0;

	virtual bool Done() = 0;
	virtual bool Failed() const = 0;

	virtual int ResultCode() const = 0;

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_.progress; }
	float SpeedKBps() const { return progress_.kBps; }
	std::string url() const { return url_; }
	virtual const Path &outfile() const = 0;

	virtual void Cancel() = 0;
	virtual bool IsCancelled() const = 0;

	// Response
	virtual Buffer &buffer() = 0;
	virtual const Buffer &buffer() const = 0;

protected:
	std::function<void(Request &)> callback_;
	RequestMethod method_;
	std::string url_;
	std::string name_;
	const char *acceptMime_ = "*/*";
	std::string userAgent_;

	net::RequestProgress progress_;
	ProgressBarMode progressBarMode_;
};

using std::shared_ptr;

class RequestManager {
public:
	~RequestManager() {
		CancelAll();
	}

	std::shared_ptr<Request> StartDownload(const std::string &url, const Path &outfile, ProgressBarMode mode, const char *acceptMime = nullptr);

	std::shared_ptr<Request> StartDownloadWithCallback(
		const std::string &url,
		const Path &outfile,
		ProgressBarMode mode,
		std::function<void(Request &)> callback,
		std::string_view name = "",
		const char *acceptMime = nullptr);

	std::shared_ptr<Request> AsyncPostWithCallback(
		const std::string &url,
		const std::string &postData,
		const std::string &postMime, // Use postMime = "application/x-www-form-urlencoded" for standard form-style posts, such as used by retroachievements. For encoding form data manually we have MultipartFormDataEncoder.
		ProgressBarMode mode,
		std::function<void(Request &)> callback,
		std::string_view name = "");

	// Drops finished downloads from the list.
	void Update();
	void CancelAll();

	void WaitForAll();
	void SetUserAgent(const std::string &userAgent) {
		userAgent_ = userAgent;
	}

private:
	static bool IsHttpsUrl(const std::string &url);

	std::vector<std::shared_ptr<Request>> downloads_;
	// These get copied to downloads_ in Update(). It's so that callbacks can add new downloads
	// while running.
	std::vector<std::shared_ptr<Request>> newDownloads_;

	std::string userAgent_;
};

inline const char *RequestMethodToString(RequestMethod method) {
	switch (method) {
	case RequestMethod::GET: return "GET";
	case RequestMethod::POST: return "POST";
	default: return "N/A";
	}
}

}  // namespace net
