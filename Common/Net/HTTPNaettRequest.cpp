#ifndef HTTPS_NOT_AVAILABLE

#include <cstring>

#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"

#include "ext/naett/naett.h"

namespace http {

HTTPSDownload::HTTPSDownload(RequestMethod method, const std::string &url, const std::string &postData, const std::string &postMime, const Path &outfile, ProgressBarMode progressBarMode, const std::string &name)
	: Download(url, name, &cancelled_), method_(method), postData_(postData), postMime_(postMime), outfile_(outfile), progressBarMode_(progressBarMode) {
}

HTTPSDownload::~HTTPSDownload() {
	Join();
}

void HTTPSDownload::Start() {
	_dbg_assert_(!req_);
	_dbg_assert_(!res_);

	std::vector<naettOption *> options;
	options.push_back(naettMethod(method_ == RequestMethod::GET ? "GET" : "POST"));
	options.push_back(naettHeader("Accept", acceptMime_));
	if (!postMime_.empty()) {
		options.push_back(naettHeader("Content-Type", postMime_.c_str()));
	}
	if (method_ == RequestMethod::POST) {
		if (!postData_.empty()) {
			// Note: Naett does not take ownership over the body.
			options.push_back(naettBody(postData_.data(), (int)postData_.size()));
		}
	} else {
		_dbg_assert_(postData_.empty());
	}
	// 30 s timeout - not sure what's reasonable?
	options.push_back(naettTimeout(30 * 1000));  // milliseconds


	const naettOption **opts = (const naettOption **)options.data();
	req_ = naettRequestWithOptions(url_.c_str(), (int)options.size(), opts);
	res_ = naettMake(req_);
}

void HTTPSDownload::Join() {
	if (!res_ || !req_)
		return;  // No pending operation.
	// Tear down.
	if (completed_ && res_) {
		_dbg_assert_(req_);
		naettClose(res_);
		naettFree(req_);
		res_ = nullptr;
		req_ = nullptr;
	} else {
		ERROR_LOG(IO, "HTTPSDownload::Join not implemented");
	}
}

bool HTTPSDownload::Done() {
	if (completed_)
		return true;

	if (!naettComplete(res_)) {
		// Not done yet, return and try again later.
		return false;
	}

	resultCode_ = naettGetStatus(res_);
	if (resultCode_ < 0) {
		// It's a naett error. Translate and handle.
		switch (resultCode_) {
		case naettConnectionError:  // -1
			ERROR_LOG(IO, "Connection error");
			break;
		case naettProtocolError:  // -2
			ERROR_LOG(IO, "Protocol error");
			break;
		case naettReadError:  // -3
			ERROR_LOG(IO, "Read error");
			break;
		case naettWriteError:  // -4
			ERROR_LOG(IO, "Write error");
			break;
		case naettGenericError:  // -4
			ERROR_LOG(IO, "Generic error");
			break;
		default:
			ERROR_LOG(IO, "Unhandled naett error %d", resultCode_);
			break;
		}
		failed_ = true;
	} else if (resultCode_ == 200) {
		int bodyLength;
		const void *body = naettGetBody(res_, &bodyLength);
		char *dest = buffer_.Append(bodyLength);
		memcpy(dest, body, bodyLength);
		if (!outfile_.empty() && !buffer_.FlushToFile(outfile_)) {
			ERROR_LOG(IO, "Failed writing download to '%s'", outfile_.c_str());
		}
	} else {
		WARN_LOG(IO, "Naett request failed: %d", resultCode_);
		failed_ = true;
	}

	completed_ = true;

	if (callback_)
		callback_(*this);
	return true;
}

}  // namespace http

#endif  // HTTPS_NOT_AVAILABLE
