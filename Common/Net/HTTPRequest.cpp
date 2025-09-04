#include "Common/Net/HTTPRequest.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/HTTPNaettRequest.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/System/OSD.h"
#include "Common/System/System.h"

namespace http {

Request::Request(RequestMethod method, std::string_view url, std::string_view name, bool *cancelled, RequestFlags flags)
	: method_(method), url_(url), name_(name), progress_(cancelled), flags_(flags) {
	INFO_LOG(Log::HTTP, "HTTP %s request: %.*s (%.*s)", RequestMethodToString(method), (int)url.size(), url.data(), (int)name.size(), name.data());

	progress_.callback = [=](int64_t bytes, int64_t contentLength, bool done) {
		std::string message;
		if (!name_.empty()) {
			message = name_;
		} else {
			std::size_t pos = url_.rfind('/');
			if (pos != std::string::npos) {
				message = url_.substr(pos + 1);
			} else {
				message = url_;
			}
		}
		if (flags_ & RequestFlags::ProgressBar) {
			if (!done) {
				g_OSD.SetProgressBar(url_, std::move(message), 0.0f, (float)contentLength, (float)bytes, flags_ & RequestFlags::ProgressBarDelayed ? 3.0f : 0.0f);  // delay 3 seconds before showing.
			} else {
				g_OSD.RemoveProgressBar(url_, Failed() ? false : true, 0.5f);
			}
		}
	};
}

}  // namespace
