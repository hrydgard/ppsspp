#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"

namespace http {

bool RequestManager::IsHttpsUrl(const std::string &url) {
	return startsWith(url, "https:");
}

std::shared_ptr<Download> RequestManager::StartDownload(const std::string &url, const Path &outfile, ProgressBarMode mode, const char *acceptMime) {
	std::shared_ptr<Download> dl;
	if (IsHttpsUrl(url)) {
		dl.reset(new HTTPSDownload(RequestMethod::GET, url, "", "", outfile, mode));
	} else {
		dl.reset(new HTTPDownload(RequestMethod::GET, url, "", "", outfile, mode));
	}

	if (!userAgent_.empty())
		dl->SetUserAgent(userAgent_);
	if (acceptMime)
		dl->SetAccept(acceptMime);
	newDownloads_.push_back(dl);
	dl->Start();
	return dl;
}

std::shared_ptr<Download> RequestManager::StartDownloadWithCallback(
	const std::string &url,
	const Path &outfile,
	ProgressBarMode mode,
	std::function<void(Download &)> callback,
	const std::string &name,
	const char *acceptMime) {
	std::shared_ptr<Download> dl;
	if (IsHttpsUrl(url)) {
		dl.reset(new HTTPSDownload(RequestMethod::GET, url, "", "", outfile, mode, name));
	} else {
		dl.reset(new HTTPDownload(RequestMethod::GET, url, "", "", outfile, mode, name));
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

std::shared_ptr<Download> RequestManager::AsyncPostWithCallback(
	const std::string &url,
	const std::string &postData,
	const std::string &postMime,
	ProgressBarMode mode,
	std::function<void(Download &)> callback,
	const std::string &name) {
	std::shared_ptr<Download> dl;
	if (IsHttpsUrl(url)) {
		dl.reset(new HTTPSDownload(RequestMethod::POST, url, postData, postMime, Path(), mode, name));
	} else {
		dl.reset(new HTTPDownload(RequestMethod::POST, url, postData, postMime, Path(), mode, name));
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
