#pragma once

#include <string>
#include <thread>
#include <string_view>
#include "Common/Net/HTTPRequestBase.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/Connection.h"

namespace http {

// Really an asynchronous request.
class HTTPRequest : public Request {
public:
	HTTPRequest(RequestMethod method, std::string_view url, std::string_view postData, std::string_view postMime, const Path &outfile, RequestFlags flags, net::ResolveFunc customResolve, std::string_view name = "");
	~HTTPRequest();

	void Start() override;
	void Join() override;

	bool Done() override { return completed_; }
	bool Failed() const override { return failed_; }

private:
	void Do();  // Actually does the download. Runs on thread.
	int Perform(const std::string &url);
	std::string RedirectLocation(const std::string &baseUrl) const;
	void SetFailed(int code);

	std::string postData_;
	std::thread thread_;
	std::string postMime_;
	bool completed_ = false;
	bool failed_ = false;
	net::ResolveFunc customResolve_;
};

}  // namespace
