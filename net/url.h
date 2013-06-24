#pragma once

#include <string>
#include <string.h>
#include <stdio.h>
#include <cstdarg>

#include "base/basictypes.h"

#if defined(_MSC_VER)
#undef snprintf
#define snprintf _snprintf
#endif

struct UrlEncoder
{
	UrlEncoder() : paramCount(0)
	{
		data.reserve(256);
	};

	void Add(const std::string &key, const std::string &value)
	{
		if (++paramCount > 1)
			data += '&';
		AppendEscaped(key);
		data += '=';
		AppendEscaped(value);
	}

	void Add(const std::string &key, const char *value)
	{
		Add(key, std::string(value));
	}

	template <typename T>
	void AddT(const std::string &key, const char *fmt, const T value)
	{
		char temp[64];
		snprintf(temp, sizeof(temp), fmt, value);
		temp[sizeof(temp) - 1] = '\0';
		Add(key, temp);
	}

	void Add(const std::string &key, const int value)
	{
		AddT(key, "%d", value);
	}

	void Add(const std::string &key, const uint32_t value)
	{
		AddT(key, "%u", value);
	}

	void Add(const std::string &key, const uint64_t value)
	{
		AddT(key, "%llu", value);
	}

	void Add(const std::string &key, const double value)
	{
		AddT(key, "%f", value);
	}

	void Add(const std::string &key, const bool value)
	{
		Add(key, value ? "true" : "false");
	}

	// Percent encoding, aka application/x-www-form-urlencoded.
	void AppendEscaped(const std::string &value)
	{
		for (size_t lastEnd = 0; lastEnd < value.length(); )
		{
			size_t pos = value.find_first_not_of(unreservedChars, lastEnd);
			if (pos == value.npos)
			{
				data += value.substr(lastEnd);
				break;
			}

			if (pos != lastEnd)
				data += value.substr(lastEnd, pos - lastEnd);
			lastEnd = pos;

			// Encode the reserved character.
			char c = value[pos];
			data += '%';
			data += hexChars[(c >> 4) & 15];
			data += hexChars[(c >> 0) & 15];
			++lastEnd;
		}
	}

	std::string &ToString()
	{
		return data;
	}

	std::string data;
	int paramCount;
	static const char *unreservedChars;
	static const char *hexChars;
};


class Url {
public:
	Url(const std::string url) : url_(url), valid_(false) {
		Split();
	}

	bool Valid() const { return valid_; }

	std::string Host() const { return host_; }
	std::string Protocol() const { return protocol_; }
	std::string Resource() const { return resource_; }

private:
	void Split();
	bool valid_;
	std::string url_;
	std::string host_;
	std::string resource_;
	std::string protocol_;
};


std::string UriDecode(const std::string & sSrc);
std::string UriEncode(const std::string & sSrc);
