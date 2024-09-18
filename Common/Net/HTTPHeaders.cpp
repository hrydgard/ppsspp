#include "Common/Net/HTTPHeaders.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>

#include "Common/Net/Sinks.h"

#include "Common/Log.h"
#include "Common/File/FileDescriptor.h"
#include "Common/StringUtils.h"

namespace http {

RequestHeader::RequestHeader() {
}

RequestHeader::~RequestHeader() {
	delete [] referer;
	delete [] user_agent;
	delete [] resource;
	delete [] params;
}

bool RequestHeader::GetParamValue(const char *param_name, std::string *value) const {
	if (!params)
		return false;
	std::string p(params);
	std::vector<std::string_view> v;
	SplitString(p, '&', v);
	for (size_t i = 0; i < v.size(); i++) {
		std::vector<std::string_view> parts;
		SplitString(v[i], '=', parts);
		DEBUG_LOG(Log::IO, "Param: %.*s Value: %.*s", (int)parts[0].size(), parts[0].data(), (int)parts[1].size(), parts[1].data());
		if (parts[0] == param_name) {
			*value = parts[1];
			return true;
		}
	}
	return false;
}

bool RequestHeader::GetOther(const char *name, std::string *value) const {
	auto it = other.find(name);
	if (it != other.end()) {
		*value = it->second;
		return true;
	}
	return false;
}

// Intended to be a mad fast parser. It's not THAT fast currently, there's still
// things to optimize, but meh.
int RequestHeader::ParseHttpHeader(const char *buffer) {
	if (first_header_) {
		// Step 1: Method
		first_header_ = false;
		if (!memcmp(buffer, "GET ", 4)) {
			method = GET;
			buffer += 4;
		} else if (!memcmp(buffer, "HEAD ", 5)) {
			method = HEAD;
			buffer += 5;
		} else if (!memcmp(buffer, "POST ", 5)) {
			method = POST;
			buffer += 5;
		} else {
			method = UNSUPPORTED;
			status = 405;
			return -1;
		}
		SkipSpace(&buffer);

		// Step 2: Resource, params (what's after the ?, if any)
		const char *endptr = strchr(buffer, ' ');
		const char *q_ptr = strchr(buffer, '?');

		int resource_name_len;
		if (q_ptr)
			resource_name_len = q_ptr - buffer;
		else
			resource_name_len = endptr - buffer;
		if (!resource_name_len) {
			status = 400;
			return -1;
		}
		resource = new char[resource_name_len + 1];
		memcpy(resource, buffer, resource_name_len);
		resource[resource_name_len] = '\0';
		if (q_ptr) {
			int param_length = endptr - q_ptr - 1;
			params = new char[param_length + 1];
			memcpy(params, q_ptr + 1, param_length);
			params[param_length] = '\0';
		}
		if (strstr(buffer, "HTTP/"))
			type = FULL;
		else
			type = SIMPLE;
		return 0;
	}

	// We have a real header to parse.
	const char *colon = strchr(buffer, ':');
	if (!colon) {
		status = 400;
		return -1;
	}

	// The header is formatted as key: value.
	int key_len = colon - buffer;
	const char *key = buffer;

	// Go to after the colon to get the value.
	buffer = colon + 1;
	SkipSpace(&buffer);
	int value_len = (int)strlen(buffer);

	if (!strncasecmp(key, "User-Agent", key_len)) {
		user_agent = new char[value_len + 1];
		memcpy(user_agent, buffer, value_len + 1);
		VERBOSE_LOG(Log::IO, "user-agent: %s", user_agent);
	} else if (!strncasecmp(key, "Referer", key_len)) {
		referer = new char[value_len + 1];
		memcpy(referer, buffer, value_len + 1);
	} else if (!strncasecmp(key, "Content-Length", key_len)) {
		content_length = atoi(buffer);
		VERBOSE_LOG(Log::IO, "Content-Length: %i", (int)content_length);
	} else {
		std::string key_str(key, key_len);
		std::transform(key_str.begin(), key_str.end(), key_str.begin(), tolower);
		other[key_str] = buffer;
	}

	return 0;
}

void RequestHeader::ParseHeaders(net::InputSink *sink) {
	int line_count = 0;
	std::string line;
	while (sink->ReadLine(line)) {
		if (line.length() == 0) {
			// Blank line, this means end of headers.
			break;
		}

		ParseHttpHeader(line.c_str());
		line_count++;
		if (type == SIMPLE) {
			// Done!
			VERBOSE_LOG(Log::IO, "Simple: Done parsing http request.");
			break;
		}
	}

	VERBOSE_LOG(Log::IO, "finished parsing request.");
	ok = line_count > 1 && resource != nullptr;
}

}  // namespace http
