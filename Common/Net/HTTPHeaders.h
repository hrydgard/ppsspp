#pragma once

#include <string>
#include <unordered_map>

#include "Common/Net/NetBuffer.h"

namespace net {
class InputSink;
};

namespace http {

class RequestHeader {
public:
	RequestHeader();
	~RequestHeader();
	// Public variables since it doesn't make sense
	// to bother with accessors for all these.
	int status = 100;

	char *referer = nullptr;  // Intentional misspelling.
	char *user_agent = nullptr;
	char *resource = nullptr;
	char *params = nullptr;

	int content_length = -1;
	std::unordered_map<std::string, std::string> other;
	enum RequestType {
		SIMPLE, FULL,
	};
	RequestType type = SIMPLE;
	enum Method {
		GET,
		HEAD,
		POST,
		UNSUPPORTED,
	};
	Method method = UNSUPPORTED;
	bool ok = false;
	void ParseHeaders(net::InputSink *sink);
	bool GetParamValue(const char *param_name, std::string *value) const;
	bool GetOther(const char *name, std::string *value) const;
private:
	int ParseHttpHeader(const char *buffer);
	bool first_header_ = true;

	DISALLOW_COPY_AND_ASSIGN(RequestHeader);
};

}  // namespace http
