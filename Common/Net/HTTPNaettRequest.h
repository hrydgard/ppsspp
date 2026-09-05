#pragma once

#include <thread>
#include <string_view>

#include "Common/Net/HTTPRequest.h"

#ifndef HTTPS_NOT_AVAILABLE

#include "ext/naett-lib/naett.h"

namespace http {

// Really an asynchronous request.
class HTTPSRequest : public Request {
public:
	HTTPSRequest(RequestMethod method, std::string_view url, std::string_view postData, std::string_view postMime, const Path &outfile, RequestFlags flags = RequestFlags::ProgressBar | RequestFlags::ProgressBarDelayed, std::string_view name = "");
	~HTTPSRequest();

	void Start() override;
	void Join() override;

	// Also acts as a Poll.
	bool Done() override;
	bool Failed() const override { return failed_; }

	// Cancelling has to reach naett, or it only changes the code we report once the transfer ends
	// on its own. See the .cpp.
	void Cancel() override;

private:
	static int WriteBodyThunk(const void *source, int bytes, void *userData);

	std::string postData_;
	std::string postMime_;
	bool completed_ = false;
	bool failed_ = false;

	// Where the response body lands. Deliberately not a member of this object: naett writes into
	// it from its own transfer thread, and that can outlive us if we're torn down before the
	// request finishes. See NaettBodySink in the .cpp.
	std::shared_ptr<struct NaettBodySink> sink_;

	// Naett state
	naettReq *req_ = nullptr;
	naettRes *res_ = nullptr;
};

}  // namespace http

#endif  // HTTPS_NOT_AVAILABLE
