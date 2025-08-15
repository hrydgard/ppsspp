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

enum class RequestFlags {
	Default = 0,
	ProgressBar = 1,
	ProgressBarDelayed = 2,
	Cached24H = 4,
	KeepInMemory = 8,
};
ENUM_CLASS_BITOPS(RequestFlags);

// Abstract request.
class Request {
public:
	Request(RequestMethod method, std::string_view url, std::string_view name, bool *cancelled, RequestFlags mode);
	virtual ~Request() {}

	void SetAccept(const char *mime) {
		acceptMime_ = mime;
	}

	void SetUserAgent(std::string_view userAgent) {
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

	// Returns 1.0 when done. That one value can be compared exactly - or just use Done().
	float Progress() const { return progress_.progress; }
	float SpeedKBps() const { return progress_.kBps; }
	std::string url() const { return url_; }

	const Path &OutFile() const { return outfile_; }
	void OverrideOutFile(const Path &path) {
		outfile_ = path;
	}
	void AddFlag(RequestFlags flag) {
		flags_ |= flag;
	}

	void Cancel() { cancelled_ = true; }
	bool IsCancelled() const { return cancelled_; }

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() { return buffer_; }
	const Buffer &buffer() const { return buffer_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const { return resultCode_; }

protected:
	RequestMethod method_;
	std::string url_;
	std::string name_;
	const char *acceptMime_ = "*/*";
	std::string userAgent_;
	Path outfile_;
	Buffer buffer_;
	bool cancelled_ = false;
	int resultCode_ = 0;
	std::vector<std::string> responseHeaders_;

	net::RequestProgress progress_;
	RequestFlags flags_;

private:
	std::function<void(Request &)> callback_;
};

using std::shared_ptr;

class RequestManager {
public:
	~RequestManager() {
		CancelAll();
	}

	// NOTE: This is the only version that supports the cache flag (for now).
	std::shared_ptr<Request> StartDownload(std::string_view url, const Path &outfile, RequestFlags flags, const char *acceptMime = nullptr);

	std::shared_ptr<Request> StartDownloadWithCallback(
		std::string_view url,
		const Path &outfile,
		RequestFlags flags,
		std::function<void(Request &)> callback,
		std::string_view name = "",
		const char *acceptMime = nullptr);

	std::shared_ptr<Request> AsyncPostWithCallback(
		std::string_view url,
		std::string_view postData,
		std::string_view postMime, // Use postMime = "application/x-www-form-urlencoded" for standard form-style posts, such as used by retroachievements. For encoding form data manually we have MultipartFormDataEncoder.
		RequestFlags flags,
		std::function<void(Request &)> callback,
		std::string_view name = "");

	// Drops finished downloads from the list.
	void Update();
	void CancelAll();

	void SetUserAgent(std::string_view userAgent) {
		userAgent_ = userAgent;
	}

	void SetCacheDir(const Path &path) {
		cacheDir_ = path;
	}

	Path UrlToCachePath(const std::string_view url);

private:
	std::vector<std::shared_ptr<Request>> downloads_;
	// These get copied to downloads_ in Update(). It's so that callbacks can add new downloads
	// while running.
	std::vector<std::shared_ptr<Request>> newDownloads_;

	std::string userAgent_;
	Path cacheDir_;
};

inline const char *RequestMethodToString(RequestMethod method) {
	switch (method) {
	case RequestMethod::GET: return "GET";
	case RequestMethod::POST: return "POST";
	default: return "N/A";
	}
}

}  // namespace net
