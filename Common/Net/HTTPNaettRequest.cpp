#include <cstring>

#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"

#include "ext/naett/naett.h"

namespace http {

HTTPSDownload::HTTPSDownload(RequestMethod method, const std::string &url, const std::string &postData, const std::string &postMime, const Path &outfile, ProgressBarMode progressBarMode, const std::string &name)
	: Download(url, name, progress_.cancelled), method_(method), postData_(postData), postMime_(postMime), outfile_(outfile), progressBarMode_(progressBarMode) {
}

HTTPSDownload::~HTTPSDownload() {
	Join();
}

void HTTPSDownload::Start() {
	_dbg_assert_(!req_);

	const char *methodStr = method_ == RequestMethod::GET ? "GET" : "POST";
	req_ = naettRequest_va(url_.c_str(), !postMime_.empty() ? 3 : 2, naettMethod(methodStr), naettHeader("accept", "application/json, text/*; q=0.9, */*; q=0.8"), naettHeader("Content-Type", postMime_.c_str()));
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

	if (naettComplete(res_)) {
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
			}
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

	return false;
}

}  // namespace
