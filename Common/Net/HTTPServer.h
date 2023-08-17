#pragma once

#include <functional>
#include <map>
#include <thread>

#include "Common/Net/HTTPHeaders.h"
#include "Common/Net/Resolve.h"

class NewThreadExecutor {
public:
	~NewThreadExecutor();
	void Run(std::function<void()> func);

private:
	std::vector<std::thread> threads_;
};

namespace net {

class InputSink;
class OutputSink;

}  // namespace net

namespace http {

class ServerRequest {
public:
	ServerRequest(int fd);
	~ServerRequest();

	const char *resource() const {
		return header_.resource;
	}

	RequestHeader::Method Method() const {
		return header_.method;
	}

	bool GetParamValue(const char *param_name, std::string *value) const {
		return header_.GetParamValue(param_name, value);
	}
	// Use lowercase.
	bool GetHeader(const char *name, std::string *value) const {
		return header_.GetOther(name, value);
	}

	net::InputSink *In() const { return in_; }
	net::OutputSink *Out() const { return out_; }

	// TODO: Remove, in favor of PartialWrite and friends.
	int fd() const { return fd_; }

	void WritePartial() const;
	void Write();
	void Close();

	bool IsOK() const { return fd_ > 0; }

	// If size is negative, no Content-Length: line is written.
	void WriteHttpResponseHeader(const char *ver, int status, int64_t size = -1, const char *mimeType = nullptr, const char *otherHeaders = nullptr) const;

private:
	net::InputSink *in_;
	net::OutputSink *out_;
	RequestHeader header_;
	int fd_;
};

// Register handlers on this class to serve stuff.
class Server {
public:
	// Takes ownership.
	Server(NewThreadExecutor *executor);
	virtual ~Server();

	typedef std::function<void(const ServerRequest &)> UrlHandlerFunc;
	typedef std::map<std::string, UrlHandlerFunc> UrlHandlerMap;

	// Runs forever, serving request. If you want to do something else than serve pages,
	// better put this on a thread. Returns false if failed to start serving, never
	// returns if successful.
	bool Run(int port);
	// May run for (significantly) longer than timeout, but won't wait longer than that
	// for a new connection to handle.
	bool RunSlice(double timeout);
	bool Listen(int port, net::DNSType type = net::DNSType::ANY);
	void Stop();

	void RegisterHandler(const char *url_path, UrlHandlerFunc handler);
	void SetFallbackHandler(UrlHandlerFunc handler);

	// If you want to customize things at a lower level than just a simple path handler,
	// then inherit and override this. Implementations should forward to HandleRequestDefault
	// if they don't recognize the url.
	virtual void HandleRequest(const ServerRequest &request);

	int Port() {
		return port_;
	}

private:
	bool Listen6(int port, bool ipv6_only);
	bool Listen4(int port);

	void HandleConnection(int conn_fd);

	// Things like default 404, etc.
	void HandleRequestDefault(const ServerRequest &request);

	// Neat built-in handlers that are tied to the server.
	void HandleListing(const ServerRequest &request);
	void Handle404(const ServerRequest &request);

	int listener_;
	int port_ = 0;

	UrlHandlerMap handlers_;
	UrlHandlerFunc fallback_;

	NewThreadExecutor *executor_;
};

}  // namespace http
