#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct addrinfo;

namespace net {

// All platforms should call these on process start and end.
void Init();
void Shutdown();

enum class DNSType {
	ANY = 0,
	IPV4 = 1,
	IPV6 = 2,
};

// This checks whether a TCP connection to host : port can be established within a given timeout.
// It does this by attempting a new, non - blocking TCP connect() and waiting for it to succeed or time out.
// The socket is closed immediately after the check.
// It does not inspect or detect existing connections from other applications; it only tests reachability by making its own connection attempt.
bool HostPortExists(const std::string &host, int port, int timeout_ms);

// Uses OS getaddrinfo to resolve an address. Blocking.
bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error, DNSType type = DNSType::ANY);
// Call this when you're done using the result of DNSResolve.
void DNSResolveFree(addrinfo *res);

// Uses tricks like getifaddrs to get a list of local IPv4 addresses. Returns false on failure, true on success (even if the list is empty).
bool GetLocalIP4List(std::vector<std::string>& IP4s);

// IP address parser.
int inet_pton(int af, const char* src, void* dst);

// Does a DNS lookup without involving the OS, so you can ask any DNS server you want, instead of
// just the one configured in the OS. Also blocking, currently.
// NOTE: This contains an internal cache that cannot be invalidated currently. TODO: At least add a timeout,
// although a process restart will clear it up anyway so maybe not that important...
bool DirectDNSLookupIPV4(const char *dnsServer, const char *host, uint32_t *ipv4_addr);

}  // namespace net
