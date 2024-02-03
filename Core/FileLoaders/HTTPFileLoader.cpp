// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/FileLoaders/HTTPFileLoader.h"

HTTPFileLoader::HTTPFileLoader(const ::Path &filename)
	: url_(filename.ToString()), progress_(&cancel_), filename_(filename) {
}

void HTTPFileLoader::Prepare() {
	std::call_once(preparedFlag_, [this](){
		client_.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

		std::vector<std::string> responseHeaders;
		Url resourceURL = url_;
		int redirectsLeft = 20;
		while (redirectsLeft > 0) {
			responseHeaders.clear();
			int code = SendHEAD(resourceURL, responseHeaders);
			if (code == -400) {
				// Already reported the error.
				return;
			}

			if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
				Disconnect();

				std::string redirectURL;
				if (http::GetHeaderValue(responseHeaders, "Location", &redirectURL)) {
					Url url(resourceURL);
					url = url.Relative(redirectURL);

					if (url.ToString() == url_.ToString() || url.ToString() == resourceURL.ToString()) {
						ERROR_LOG(LOADER, "HTTP request failed, hit a redirect loop");
						latestError_ = "Could not connect (redirect loop)";
						return;
					}

					resourceURL = url;
					redirectsLeft--;
					continue;
				}

				// No Location header?
				ERROR_LOG(LOADER, "HTTP request failed, invalid redirect");
				latestError_ = "Could not connect (invalid response)";
				return;
			}

			if (code != 200) {
				// Leave size at 0, invalid.
				ERROR_LOG(LOADER, "HTTP request failed, got %03d for %s", code, filename_.c_str());
				latestError_ = "Could not connect (invalid response)";
				Disconnect();
				return;
			}

			// We got a good, non-redirect response.
			redirectsLeft = 0;
			url_ = resourceURL;
		}

		// TODO: Expire cache via ETag, etc.
		bool acceptsRange = false;
		for (std::string header : responseHeaders) {
			if (startsWithNoCase(header, "Content-Length:")) {
				size_t size_pos = header.find_first_of(' ');
				if (size_pos != header.npos) {
					size_pos = header.find_first_not_of(' ', size_pos);
				}
				if (size_pos != header.npos) {
					filesize_ = atoll(&header[size_pos]);
				}
			}
			if (startsWithNoCase(header, "Accept-Ranges:")) {
				std::string lowerHeader = header;
				std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), tolower);
				// TODO: Delimited.
				if (lowerHeader.find("bytes") != lowerHeader.npos) {
					acceptsRange = true;
				}
			}
		}

		// TODO: Keepalive instead.
		Disconnect();

		if (!acceptsRange) {
			WARN_LOG(LOADER, "HTTP server did not advertise support for range requests.");
		}
		if (filesize_ == 0) {
			ERROR_LOG(LOADER, "Could not determine file size for %s", filename_.c_str());
		}

		// If we didn't end up with a filesize_ (e.g. chunked response), give up.  File invalid.
	});
}

int HTTPFileLoader::SendHEAD(const Url &url, std::vector<std::string> &responseHeaders) {
	if (!url.Valid()) {
		ERROR_LOG(LOADER, "HTTP request failed, invalid URL: '%s'", url.ToString().c_str());
		latestError_ = "Invalid URL";
		return -400;
	}

	if (!client_.Resolve(url.Host().c_str(), url.Port())) {
		ERROR_LOG(LOADER, "HTTP request failed, unable to resolve: |%s| port %d", url.Host().c_str(), url.Port());
		latestError_ = "Could not connect (name not resolved)";
		return -400;
	}

	double timeout = 20.0;

	client_.SetDataTimeout(timeout);
	Connect(10.0);
	if (!connected_) {
		ERROR_LOG(LOADER, "HTTP request failed, failed to connect: %s port %d (resource: '%s')", url.Host().c_str(), url.Port(), url.Resource().c_str());
		latestError_ = "Could not connect (refused to connect)";
		return -400;
	}

	http::RequestParams req(url.Resource(), "*/*");
	int err = client_.SendRequest("HEAD", req, nullptr, &progress_);
	if (err < 0) {
		ERROR_LOG(LOADER, "HTTP request failed, failed to send request: %s port %d", url.Host().c_str(), url.Port());
		latestError_ = "Could not connect (could not request data)";
		Disconnect();
		return -400;
	}

	net::Buffer readbuf;
	return client_.ReadResponseHeaders(&readbuf, responseHeaders, &progress_);
}

HTTPFileLoader::~HTTPFileLoader() {
	Disconnect();
}

bool HTTPFileLoader::Exists() {
	Prepare();
	return url_.Valid() && filesize_ > 0;
}

bool HTTPFileLoader::ExistsFast() {
	return url_.Valid();
}

bool HTTPFileLoader::IsDirectory() {
	// Only files.
	return false;
}

s64 HTTPFileLoader::FileSize() {
	Prepare();
	return filesize_;
}

Path HTTPFileLoader::GetPath() const {
	return filename_;
}

size_t HTTPFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	Prepare();
	std::lock_guard<std::mutex> guard(readAtMutex_);

	s64 absoluteEnd = std::min(absolutePos + (s64)bytes, filesize_);
	if (absolutePos >= filesize_ || bytes == 0) {
		// Read outside of the file or no read at all, just fail immediately.
		return 0;
	}

	Connect(10.0);
	if (!connected_) {
		return 0;
	}

	char requestHeaders[4096];
	// Note that the Range header is *inclusive*.
	snprintf(requestHeaders, sizeof(requestHeaders),
		"Range: bytes=%lld-%lld\r\n", absolutePos, absoluteEnd - 1);

	http::RequestParams req(url_.Resource(), "*/*");
	int err = client_.SendRequest("GET", req, requestHeaders, &progress_);
	if (err < 0) {
		latestError_ = "Invalid response reading data";
		Disconnect();
		return 0;
	}

	net::Buffer readbuf;
	std::vector<std::string> responseHeaders;
	int code = client_.ReadResponseHeaders(&readbuf, responseHeaders, &progress_);
	if (code != 206) {
		ERROR_LOG(LOADER, "HTTP server did not respond with range, received code=%03d", code);
		latestError_ = "Invalid response reading data";
		Disconnect();
		return 0;
	}

	// TODO: Expire cache via ETag, etc.
	// We don't support multipart/byteranges responses.
	bool supportedResponse = false;
	for (std::string header : responseHeaders) {
		if (startsWithNoCase(header, "Content-Range:")) {
			// TODO: More correctness.  Whitespace can be missing or different.
			s64 first = -1, last = -1, total = -1;
			std::string lowerHeader = header;
			std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), tolower);
			if (sscanf(lowerHeader.c_str(), "content-range: bytes %lld-%lld/%lld", &first, &last, &total) >= 2) {
				if (first == absolutePos && last == absoluteEnd - 1) {
					supportedResponse = true;
				} else {
					ERROR_LOG(LOADER, "Unexpected HTTP range: got %lld-%lld, wanted %lld-%lld.", first, last, absolutePos, absoluteEnd - 1);
				}
			} else {
				ERROR_LOG(LOADER, "Unexpected HTTP range response: %s", header.c_str());
			}
		}
	}

	// TODO: Would be nice to read directly.
	net::Buffer output;
	int res = client_.ReadResponseEntity(&readbuf, responseHeaders, &output, &progress_);
	if (res != 0) {
		ERROR_LOG(LOADER, "Unable to read HTTP response entity: %d", res);
		// Let's take anything we got anyway.  Not worse than returning nothing?
	}

	// TODO: Keepalive instead.
	Disconnect();

	if (!supportedResponse) {
		ERROR_LOG(LOADER, "HTTP server did not respond with the range we wanted.");
		latestError_ = "Invalid response reading data";
		return 0;
	}

	size_t readBytes = output.size();
	output.Take(readBytes, (char *)data);
	filepos_ = absolutePos + readBytes;
	return readBytes;
}

void HTTPFileLoader::Connect(double timeout) {
	if (!connected_) {
		cancel_ = false;
		connected_ = client_.Connect(3, timeout, &cancel_);
	}
}
