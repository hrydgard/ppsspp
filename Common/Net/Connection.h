#pragma once

#include <functional>
#include <cstdint>
#include <string>

#include "Common/Net/Resolve.h"

namespace net {

class Connection {
public:
	virtual ~Connection();

	explicit Connection(ResolveFunc func) : customResolve_(func) {}

	// Inits the sockaddr_in.
	bool Resolve(const char *host, int port, DNSType type = DNSType::ANY);

	bool Connect(int maxTries = 2, double timeout = 20.0f, bool *cancelConnect = nullptr);
	void Disconnect();

	// Only to be used for bring-up and debugging.
	uintptr_t sock() const { return sock_; }

protected:
	// Store the remote host here, so we can send it along through HTTP/1.1 requests.
	// TODO: Move to http::client?
	std::string host_;
	int port_ = -1;

	addrinfo *resolved_ = nullptr;

private:
	uintptr_t sock_ = -1;
	ResolveFunc customResolve_;
};

}  // namespace net
