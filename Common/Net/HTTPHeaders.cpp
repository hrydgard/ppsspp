#include "Common/Net/HTTPHeaders.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>  // std::transform

#include "Common/Net/Sinks.h"

#include "Common/Log.h"
#include "Common/File/FileDescriptor.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

namespace http {

// Limits on a request's header block, so a client can't hold a connection or grow memory forever.
static constexpr int MAX_HEADER_LINES = 100;
static constexpr size_t MAX_HEADER_BYTES = 64 * 1024;
static constexpr double MAX_HEADER_SECONDS = 10.0;

static bool KeyIs(const char *key, int key_len, const char *name) {
	return key_len == (int)strlen(name) && !strncasecmp(key, name, key_len);
}

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
		if (parts.size() < 2) {
			// A parameter with no '=' at all, like "?foo". SplitString hands back a single element
			// for that, so parts[1] would be reading off the end of the vector.
			continue;
		}
		DEBUG_LOG(Log::HTTP, "Param: %.*s Value: %.*s", STR_VIEW(parts[0]), STR_VIEW(parts[1]));
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

int RequestHeader::ParseHttpHeader(const char *buffer) {
	if (first_header_) {
		// Step 1: Method
		first_header_ = false;
		if (!strncmp(buffer, "GET ", 4)) {
			method = GET;
			buffer += 4;
		} else if (!strncmp(buffer, "HEAD ", 5)) {
			method = HEAD;
			buffer += 5;
		} else if (!strncmp(buffer, "POST ", 5)) {
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
		if (!endptr) {
			// No trailing space. Fall back to
			// the end of the line instead of leaving endptr null.
			// Nemo's repro (python):
			// import socket
			// SERVER_IP = "127.0.0.1"
			// PORT = 65199  # my port
			// message = "GET /\n"
			// client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
			// client_socket.connect((SERVER_IP, PORT)); client_socket.sendall(message.encode('utf-8'))
			endptr = buffer + strlen(buffer);
		}

		int resource_name_len;
		if (q_ptr)
			resource_name_len = (int)(q_ptr - buffer);
		else
			resource_name_len = (int)(endptr - buffer);
		if (!resource_name_len) {
			status = 400;
			return -1;
		}
		resource = new char[resource_name_len + 1];
		memcpy(resource, buffer, resource_name_len);
		resource[resource_name_len] = '\0';
		if (q_ptr) {
			int param_length = (int)(endptr - q_ptr - 1);
			if (param_length < 0)  // This is likely just paranoia.
				param_length = 0;
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

	if (KeyIs(key, key_len, "User-Agent")) {
		delete[] user_agent;
		user_agent = new char[value_len + 1];
		memcpy(user_agent, buffer, value_len + 1);
		VERBOSE_LOG(Log::HTTP, "user-agent: %s", user_agent);
	} else if (KeyIs(key, key_len, "Referer")) {
		delete[] referer;
		referer = new char[value_len + 1];
		memcpy(referer, buffer, value_len + 1);
	} else if (KeyIs(key, key_len, "Content-Length")) {
		content_length = atoi(buffer);
		VERBOSE_LOG(Log::HTTP, "Content-Length: %i", (int)content_length);
	} else {
		std::string key_str(key, key_len);
		std::transform(key_str.begin(), key_str.end(), key_str.begin(), tolower);
		other[key_str] = buffer;
	}

	return 0;
}

void RequestHeader::ParseHeaders(net::InputSink *sink) {
	int line_count = 0;
	size_t total_bytes = 0;
	const double deadline = time_now_d() + MAX_HEADER_SECONDS;
	std::string line;
	while (sink->ReadLine(line)) {
		if (line.length() == 0) {
			// Blank line, this means end of headers.
			break;
		}
		total_bytes += line.length();
		if (line_count >= MAX_HEADER_LINES || total_bytes > MAX_HEADER_BYTES || time_now_d() > deadline) {
			WARN_LOG(Log::HTTP, "Request headers too long, dropping the request");
			status = 431;
			ok = false;
			return;
		}

		ParseHttpHeader(line.c_str());
		line_count++;
		if (type == SIMPLE) {
			// Done!
			VERBOSE_LOG(Log::HTTP, "Simple: Done parsing http request.");
			break;
		}
	}

	VERBOSE_LOG(Log::HTTP, "finished parsing request.");
	ok = line_count > 1 && resource != nullptr;
}

}  // namespace http
