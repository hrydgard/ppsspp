#ifndef HTTPS_NOT_AVAILABLE

#include <cstring>

#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

#include "ext/naett/naett.h"

namespace http {

HTTPSRequest::HTTPSRequest(RequestMethod method, const std::string &url, const std::string &postData, const std::string &postMime, const Path &outfile, ProgressBarMode progressBarMode, std::string_view name)
	: Request(method, url, name, &cancelled_, progressBarMode), method_(method), postData_(postData), postMime_(postMime), outfile_(outfile) {
}

HTTPSRequest::~HTTPSRequest() {
	Join();
}

void HTTPSRequest::Start() {
	_dbg_assert_(!req_);
	_dbg_assert_(!res_);

	std::vector<naettOption *> options;
	options.push_back(naettMethod(method_ == RequestMethod::GET ? "GET" : "POST"));
	options.push_back(naettHeader("Accept", acceptMime_));
	options.push_back(naettUserAgent(userAgent_.c_str()));
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

	progress_.Update(0, 0, false);
}

void HTTPSRequest::Join() {
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
		ERROR_LOG(Log::IO, "HTTPSDownload::Join not implemented");
	}
}

bool HTTPSRequest::Done() {
	if (completed_)
		return true;

	if (!naettComplete(res_)) {
		int total = 0;
		int size = naettGetTotalBytesRead(res_, &total);
		progress_.Update(size, total, false);
		return false;
	}

	// -1000 is a code specified by us to represent cancellation, that is unlikely to ever collide with naett error codes.
	resultCode_ = IsCancelled() ? -1000 : naettGetStatus(res_);
	int bodyLength;
	const void *body = naettGetBody(res_, &bodyLength);
	char *dest = buffer_.Append(bodyLength);
	memcpy(dest, body, bodyLength);
	if (resultCode_ < 0) {
		// It's a naett error. Translate and handle.
		switch (resultCode_) {
		case naettConnectionError:  // -1
			ERROR_LOG(Log::IO, "Connection error");
			break;
		case naettProtocolError:  // -2
			ERROR_LOG(Log::IO, "Protocol error");
			break;
		case naettReadError:  // -3
			ERROR_LOG(Log::IO, "Read error");
			break;
		case naettWriteError:  // -4
			ERROR_LOG(Log::IO, "Write error");
			break;
		case naettGenericError:  // -5
			ERROR_LOG(Log::IO, "Generic error");
			break;
		default:
			ERROR_LOG(Log::IO, "Unhandled naett error %d", resultCode_);
			break;
		}
		failed_ = true;
		progress_.Update(bodyLength, bodyLength, true);
	} else if (resultCode_ == 200) {
		if (!outfile_.empty() && !buffer_.FlushToFile(outfile_)) {
			ERROR_LOG(Log::IO, "Failed writing download to '%s'", outfile_.c_str());
		}
		progress_.Update(bodyLength, bodyLength, true);
	} else {
		WARN_LOG(Log::IO, "Naett request failed: %d", resultCode_);
		failed_ = true;
		progress_.Update(0, 0, true);
	}

	completed_ = true;

	// The callback will be called later.
	return true;
}

}  // namespace http

#endif  // HTTPS_NOT_AVAILABLE
