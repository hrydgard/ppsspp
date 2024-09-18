#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/System/OSD.h"
#include "Common/System/System.h"

namespace http {

Request::Request(RequestMethod method, const std::string &url, std::string_view name, bool *cancelled, ProgressBarMode mode) : method_(method), url_(url), name_(name), progress_(cancelled), progressBarMode_(mode) {
	INFO_LOG(Log::HTTP, "HTTP %s request: %s (%.*s)", RequestMethodToString(method), url.c_str(), (int)name.size(), name.data());

	progress_.callback = [=](int64_t bytes, int64_t contentLength, bool done) {
		std::string message;
		if (!name_.empty()) {
			message = name_;
		} else {
			std::size_t pos = url_.rfind('/');
			if (pos != std::string::npos) {
				message = url_.substr(pos + 1);
			} else {
				message = url_;
			}
		}
		if (progressBarMode_ != ProgressBarMode::NONE) {
			if (!done) {
				g_OSD.SetProgressBar(url_, std::move(message), 0.0f, (float)contentLength, (float)bytes, progressBarMode_ == ProgressBarMode::DELAYED ? 3.0f : 0.0f);  // delay 3 seconds before showing.
			} else {
				g_OSD.RemoveProgressBar(url_, Failed() ? false : true, 0.5f);
			}
		}
	};
}

bool RequestManager::IsHttpsUrl(const std::string &url) {
	return startsWith(url, "https:");
}

std::shared_ptr<Request> RequestManager::StartDownload(const std::string &url, const Path &outfile, ProgressBarMode mode, const char *acceptMime) {
	std::shared_ptr<Request> dl;
	if (IsHttpsUrl(url) && System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
#ifndef HTTPS_NOT_AVAILABLE
		dl.reset(new HTTPSRequest(RequestMethod::GET, url, "", "", outfile, mode));
#else
		return std::shared_ptr<Request>();
#endif
	} else {
		dl.reset(new HTTPRequest(RequestMethod::GET, url, "", "", outfile, mode));
	}

	if (!userAgent_.empty())
		dl->SetUserAgent(userAgent_);
	if (acceptMime)
		dl->SetAccept(acceptMime);
	newDownloads_.push_back(dl);
	dl->Start();
	return dl;
}

std::shared_ptr<Request> RequestManager::StartDownloadWithCallback(
	const std::string &url,
	const Path &outfile,
	ProgressBarMode mode,
	std::function<void(Request &)> callback,
	std::string_view name,
	const char *acceptMime) {
	std::shared_ptr<Request> dl;
	if (IsHttpsUrl(url) && System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
#ifndef HTTPS_NOT_AVAILABLE
		dl.reset(new HTTPSRequest(RequestMethod::GET, url, "", "", outfile, mode, name));
#else
		return std::shared_ptr<Request>();
#endif
	} else {
		dl.reset(new HTTPRequest(RequestMethod::GET, url, "", "", outfile, mode, name));
	}
	if (!userAgent_.empty())
		dl->SetUserAgent(userAgent_);
	if (acceptMime)
		dl->SetAccept(acceptMime);
	dl->SetCallback(callback);
	newDownloads_.push_back(dl);
	dl->Start();
	return dl;
}

std::shared_ptr<Request> RequestManager::AsyncPostWithCallback(
	const std::string &url,
	const std::string &postData,
	const std::string &postMime,
	ProgressBarMode mode,
	std::function<void(Request &)> callback,
	std::string_view name) {
	std::shared_ptr<Request> dl;
	if (IsHttpsUrl(url) && System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
#ifndef HTTPS_NOT_AVAILABLE
		dl.reset(new HTTPSRequest(RequestMethod::POST, url, postData, postMime, Path(), mode, name));
#else
		return std::shared_ptr<Request>();
#endif
	} else {
		dl.reset(new HTTPRequest(RequestMethod::POST, url, postData, postMime, Path(), mode, name));
	}
	if (!userAgent_.empty())
		dl->SetUserAgent(userAgent_);
	dl->SetCallback(callback);
	newDownloads_.push_back(dl);
	dl->Start();
	return dl;
}

void RequestManager::Update() {
	for (auto iter : newDownloads_) {
		downloads_.push_back(iter);
	}
	newDownloads_.clear();

restart:
	for (size_t i = 0; i < downloads_.size(); i++) {
		auto dl = downloads_[i];
		if (dl->Done()) {
			dl->RunCallback();
			dl->Join();
			downloads_.erase(downloads_.begin() + i);
			goto restart;
		}
	}
}

void RequestManager::WaitForAll() {
	// TODO: Should lock? Though, OK if called from main thread, where Update() is called from.
	while (!downloads_.empty()) {
		Update();
		sleep_ms(10);
	}
}

void RequestManager::CancelAll() {
	for (size_t i = 0; i < downloads_.size(); i++) {
		downloads_[i]->Cancel();
	}
	for (size_t i = 0; i < downloads_.size(); i++) {
		downloads_[i]->Join();
	}
	downloads_.clear();
}

}  // namespace
