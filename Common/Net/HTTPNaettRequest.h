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
	HTTPSRequest(RequestMethod method, const std::string &url, const std::string &postData, const std::string &postMime, const Path &outfile, ProgressBarMode progressBarMode = ProgressBarMode::DELAYED, std::string_view name = "");
	~HTTPSRequest();

	void Start() override;
	void Join() override;

	// Also acts as a Poll.
	bool Done() override;
	bool Failed() const override { return failed_; }

	// NOTE! The value of ResultCode is INVALID until Done() returns true.
	int ResultCode() const override { return resultCode_; }

	const Path &outfile() const override { return outfile_; }

	// If not downloading to a file, access this to get the result.
	Buffer &buffer() override { return buffer_; }
	const Buffer &buffer() const override { return buffer_; }

	void Cancel() override {
		cancelled_ = true;
	}

	bool IsCancelled() const override {
		return cancelled_;
	}

private:
	RequestMethod method_;
	std::string postData_;
	Buffer buffer_;
	std::vector<std::string> responseHeaders_;
	Path outfile_;
	std::string postMime_;
	int resultCode_ = 0;
	bool completed_ = false;
	bool failed_ = false;
	bool cancelled_ = false;
	bool joined_ = false;

	// Naett state
	naettReq *req_ = nullptr;
	naettRes *res_ = nullptr;
};

}  // namespace http

#endif  // HTTPS_NOT_AVAILABLE
