#pragma once

#include <thread>
#include <string_view>

#include "Common/Net/HTTPRequest.h"

#ifndef HTTPS_NOT_AVAILABLE

#include "ext/naett/naett.h"

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

private:
	RequestMethod method_;
	std::string postData_;
	std::string postMime_;
	bool completed_ = false;
	bool failed_ = false;

	// Naett state
	naettReq *req_ = nullptr;
	naettRes *res_ = nullptr;
};

}  // namespace http

#endif  // HTTPS_NOT_AVAILABLE
