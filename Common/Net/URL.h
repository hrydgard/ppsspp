#pragma once

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <vector>

#if defined(_MSC_VER) && _MSC_VER < 1900
#undef snprintf
#define snprintf _snprintf
#endif

struct UrlEncoder
{
	virtual ~UrlEncoder() {}

	UrlEncoder() : paramCount(0)
	{
		data.reserve(256);
	}

	virtual void Add(const std::string &key, const std::string &value)
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

	virtual void Finish()
	{
	}

	const std::string &ToString() const
	{
		return data;
	}

	virtual std::string GetMimeType() const
	{
		return "application/x-www-form-urlencoded";
	}

protected:
	template <typename T>
	void AddT(const std::string &key, const char *fmt, const T value)
	{
		char temp[64];
		snprintf(temp, sizeof(temp), fmt, value);
		temp[sizeof(temp) - 1] = '\0';
		Add(key, temp);
	}

	// Percent encoding, aka application/x-www-form-urlencoded.
	void AppendEscaped(const std::string &value);

	std::string data;
	int paramCount;
};


// Stores everything in memory, not super efficient.
// Easy to swap out for a UrlEncoder.
struct MultipartFormDataEncoder : UrlEncoder
{
	MultipartFormDataEncoder() : UrlEncoder()
	{
		data.reserve(8192);
		int r1 = rand();
		int r2 = rand();
		char temp[256];
		snprintf(temp, sizeof(temp), "NATIVE-DATA-BOUNDARY-%08x%08x-%d", r1, r2, seq++);
		boundary = temp;
	}

	void Add(const std::string &key, const std::string &value) override {
		Add(key, value, "", "");
	}

	void Add(const std::string &key, const std::string &value, const std::string &filename, const std::string &mimeType)
	{
		data += "--" + boundary + "\r\n";
		data += "Content-Disposition: form-data; name=\"" + key + "\"";
		if (!filename.empty())
			data += "; filename=\"" + filename + "\"";
		data += "\r\n";
		if (!mimeType.empty())
			data += "Content-Type: " + mimeType + "\r\n";
		char temp[64];
		snprintf(temp, sizeof(temp), "Content-Length: %d\r\n", (int)value.size());
		data += temp;
		data += "Content-Transfer-Encoding: binary\r\n";
		data += "\r\n";

		data += value;
		data += "\r\n";
	}

	void Add(const std::string &key, const std::vector<uint8_t> &value, const std::string &filename, const std::string &mimeType)
	{
		Add(key, std::string((const char *)&value[0], value.size()), filename, mimeType);
	}

	void Finish() override {
		data += "--" + boundary + "--";
	}

	std::string GetMimeType() const override {
		return "multipart/form-data; boundary=\"" + boundary + "\"";
	}

protected:
	std::string boundary;

	static int seq;
};


class Url {
public:
	Url(const std::string url) : valid_(false), url_(url) {
		Split();
	}

	bool Valid() const { return valid_; }

	// Host = Hostname:Port, or just Hostname.
	std::string Host() const { return host_; }
	int Port() const { return port_; }
	std::string Protocol() const { return protocol_; }
	std::string Resource() const { return resource_; }

	Url Relative(const std::string &next) const;

	std::string ToString() const;

private:
	void Split();
	bool valid_;
	std::string url_;
	std::string host_;
	std::string resource_;
	std::string protocol_;
	int port_;
};


std::string UriDecode(std::string_view sSrc);
std::string UriEncode(std::string_view sSrc);
