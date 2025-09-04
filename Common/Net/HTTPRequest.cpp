#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/URL.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"

namespace http {

HTTPRequest::HTTPRequest(RequestMethod method, std::string_view url, std::string_view postData, std::string_view postMime, const Path &outfile, RequestFlags flags, net::ResolveFunc customResolve, std::string_view name)
	: Request(method, url, name, &cancelled_, flags), postData_(postData), postMime_(postMime), customResolve_(customResolve) {
	outfile_ = outfile;
}

HTTPRequest::~HTTPRequest() {
	g_OSD.RemoveProgressBar(url_, !failed_, 0.5f);

	if (thread_.joinable()) {
		_dbg_assert_msg_(false, "Download destructed without join");
	}
}

void HTTPRequest::Start() {
	thread_ = std::thread([this] { Do(); });
}

void HTTPRequest::Join() {
	if (!thread_.joinable()) {
		ERROR_LOG(Log::HTTP, "Already joined thread!");
		_dbg_assert_(false);
	}
	thread_.join();
}

void HTTPRequest::SetFailed(int code) {
	failed_ = true;
	progress_.Update(0, 0, true);
	completed_ = true;
}

int HTTPRequest::Perform(const std::string &url) {
	Url fileUrl(url);
	if (!fileUrl.Valid()) {
		return -1;
	}

	http::Client client(customResolve_);
	if (!userAgent_.empty()) {
		client.SetUserAgent(userAgent_);
	}

	if (!client.Resolve(fileUrl.Host().c_str(), fileUrl.Port())) {
		ERROR_LOG(Log::HTTP, "Failed resolving %s", url.c_str());
		return -1;
	}

	if (cancelled_) {
		return -1;
	}

	if (!client.Connect(2, 20.0, &cancelled_)) {
		ERROR_LOG(Log::HTTP, "Failed connecting to server or cancelled (=%d).", cancelled_);
		return -1;
	}

	if (cancelled_) {
		return -1;
	}

	RequestParams req(fileUrl.Resource(), acceptMime_);
	if (method_ == RequestMethod::GET) {
		return client.GET(req, &buffer_, responseHeaders_, &progress_);
	} else {
		return client.POST(req, postData_, postMime_, &buffer_, &progress_);
	}
}

std::string HTTPRequest::RedirectLocation(const std::string &baseUrl) const {
	std::string redirectUrl;
	if (GetHeaderValue(responseHeaders_, "Location", &redirectUrl)) {
		Url url(baseUrl);
		url = url.Relative(redirectUrl);
		redirectUrl = url.ToString();
	}
	return redirectUrl;
}

void HTTPRequest::Do() {
	SetCurrentThreadName("HTTPDownload::Do");

	AndroidJNIThreadContext jniContext;
	resultCode_ = 0;

	std::string downloadURL = url_;
	while (resultCode_ == 0) {
		// This is where the new request is performed.
		int resultCode = Perform(downloadURL);
		if (resultCode == -1) {
			SetFailed(resultCode);
			return;
		}

		if (resultCode == 301 || resultCode == 302 || resultCode == 303 || resultCode == 307 || resultCode == 308) {
			std::string redirectURL = RedirectLocation(downloadURL);
			if (redirectURL.empty()) {
				ERROR_LOG(Log::HTTP, "Could not find Location header for redirect");
				resultCode_ = resultCode;
			} else if (redirectURL == downloadURL || redirectURL == url_) {
				// Simple loop detected, bail out.
				resultCode_ = resultCode;
			}

			// Perform the next GET.
			if (resultCode_ == 0) {
				INFO_LOG(Log::HTTP, "Download of %s redirected to %s", downloadURL.c_str(), redirectURL.c_str());
				buffer_.clear();
				responseHeaders_.clear();
			}
			downloadURL = redirectURL;
			continue;
		}

		if (resultCode == 200) {
			INFO_LOG(Log::HTTP, "Completed requesting %s (storing result to %s)", url_.c_str(), outfile_.empty() ? "memory" : outfile_.c_str());
			bool clear = !(flags_ & RequestFlags::KeepInMemory);
			if (!outfile_.empty() && !buffer_.FlushToFile(outfile_, clear)) {
				ERROR_LOG(Log::HTTP, "Failed writing download to '%s'", outfile_.c_str());
			}
		} else {
			ERROR_LOG(Log::HTTP, "Error requesting '%s' (storing result to '%s'): %i", url_.c_str(), outfile_.empty() ? "memory" : outfile_.c_str(), resultCode);
		}
		resultCode_ = resultCode;
	}

	// Set this last to ensure no race conditions when checking Done. Users must always check
	// Done before looking at the result code.
	completed_ = true;
}

}  // namespace http
