#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct addrinfo;

namespace net {

// Strictly only required on Win32, but all platforms should call it.
void Init();
void Shutdown();

enum class DNSType {
	ANY = 0,
	IPV4 = 1,
	IPV6 = 2,
};

bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error, DNSType type = DNSType::ANY);
void DNSResolveFree(addrinfo *res);
bool GetLocalIP4List(std::vector<std::string>& IP4s);

int inet_pton(int af, const char* src, void* dst);

// Does a DNS lookup without involving the OS, so you can hit any DNS server.
bool DirectDNSLookupIPV4(const char *dnsServer, const char *host, uint32_t *ipv4_addr);

}  // namespace net
