#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/System/OSD.h"
#include "Common/System/System.h"

namespace http {

Request::Request(RequestMethod method, std::string_view url, std::string_view name, bool *cancelled, RequestFlags mode)
	: method_(method), url_(url), name_(name), progress_(cancelled), flags_(mode) {
	INFO_LOG(Log::HTTP, "HTTP %s request: %.*s (%.*s)", RequestMethodToString(method), (int)url.size(), url.data(), (int)name.size(), name.data());

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
		if (flags_ & RequestFlags::ProgressBar) {
			if (!done) {
				g_OSD.SetProgressBar(url_, std::move(message), 0.0f, (float)contentLength, (float)bytes, flags_ & RequestFlags::ProgressBarDelayed ? 3.0f : 0.0f);  // delay 3 seconds before showing.
			} else {
				g_OSD.RemoveProgressBar(url_, Failed() ? false : true, 0.5f);
			}
		}
	};
}

static bool IsHttpsUrl(std::string_view url) {
	return startsWith(url, "https:");
}

std::shared_ptr<Request> CreateRequest(RequestMethod method, std::string_view url, std::string_view postdata, std::string_view postMime, const Path &outfile, RequestFlags flags, std::string_view name) {
	if (IsHttpsUrl(url) && System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
#ifndef HTTPS_NOT_AVAILABLE
		return std::shared_ptr<Request>(new HTTPSRequest(method, url, postdata, postMime, outfile, flags, name));
#else
		return std::shared_ptr<Request>();
#endif
	} else {
		return std::shared_ptr<Request>(new HTTPRequest(method, url, postdata, postMime, outfile, flags, name));
	}
}

std::shared_ptr<Request> RequestManager::StartDownload(std::string_view url, const Path &outfile, RequestFlags flags, const char *acceptMime) {
	std::shared_ptr<Request> dl = CreateRequest(RequestMethod::GET, url, "", "", outfile, flags, "");

	if (!userAgent_.empty())
		dl->SetUserAgent(userAgent_);
	if (acceptMime)
		dl->SetAccept(acceptMime);
	newDownloads_.push_back(dl);
	dl->Start();
	return dl;
}

std::shared_ptr<Request> RequestManager::StartDownloadWithCallback(
	std::string_view url,
	const Path &outfile,
	RequestFlags flags,
	std::function<void(Request &)> callback,
	std::string_view name,
	const char *acceptMime) {
	std::shared_ptr<Request> dl = CreateRequest(RequestMethod::GET, url, "", "", outfile, flags, name);

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
	std::string_view url,
	std::string_view postData,
	std::string_view postMime,
	RequestFlags flags,
	std::function<void(Request &)> callback,
	std::string_view name) {
	std::shared_ptr<Request> dl = CreateRequest(RequestMethod::POST, url, postData, postMime, Path(), flags, name);
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
