#pragma once

#include <memory>
#include <string_view>
#include "Common/Net/HTTPRequestBase.h"
#include "Common/Net/NetBuffer.h"

namespace http {

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
