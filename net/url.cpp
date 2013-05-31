#include "base/logging.h"
#include "net/url.h"

const char *UrlEncoder::unreservedChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
const char *UrlEncoder::hexChars = "0123456789ABCDEF";

void Url::Split() {
	size_t colonSlashSlash = url_.find("://");
	if (colonSlashSlash == std::string::npos) {
		ELOG("Invalid URL: %s", url_.c_str());
		return;
	}

	protocol_ = url_.substr(0, colonSlashSlash);

	size_t sep = url_.find('/', colonSlashSlash + 3);

	host_ = url_.substr(colonSlashSlash + 3, sep - colonSlashSlash - 3);
	resource_ = url_.substr(sep);  // include the slash!

	valid_ = protocol_.size() > 1 && host_.size() > 1;
}