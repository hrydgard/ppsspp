#include "ppsspp_config.h"
#include "Common/Net/Resolve.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Net/SocketCompat.h"

#ifndef HTTPS_NOT_AVAILABLE
#include "ext/naett/naett.h"
#endif

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
extern JavaVM *gJvm;
#endif

namespace net {

static bool g_naettInitialized;

void Init()
{
#ifdef _WIN32
	// WSA does its own internal reference counting, no need to keep track of if we inited or not.
	WSADATA wsaData = {0};
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	if (!g_naettInitialized) {
#ifndef HTTPS_NOT_AVAILABLE
#if PPSSPP_PLATFORM(ANDROID)
		_assert_(gJvm != nullptr);
		naettInit(gJvm);
#else
		naettInit(NULL);
#endif
#endif
		g_naettInitialized = true;
	}
}

void Shutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

// NOTE: Due to the nature of getaddrinfo, this can block indefinitely. Not good.
bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error, DNSType type) {
#if PPSSPP_PLATFORM(SWITCH)
	// Force IPv4 lookups.
	if (type == DNSType::IPV6) {
		*res = nullptr;
		return false;
	} else if (type == DNSType::ANY) {
		type = DNSType::IPV4;
	}
#endif

	addrinfo hints = {0};
	// TODO: Might be uses to lookup other values.
	hints.ai_socktype = SOCK_STREAM;
#ifdef __ANDROID__
	hints.ai_flags = AI_ADDRCONFIG;
#else
	// AI_V4MAPPED seems to have issues on some platforms, not sure we should include it:
	// http://stackoverflow.com/questions/1408030/what-is-the-purpose-of-the-ai-v4mapped-flag-in-getaddrinfo
	hints.ai_flags = /*AI_V4MAPPED |*/ AI_ADDRCONFIG;
#endif
	hints.ai_protocol = 0;
	if (type == DNSType::IPV4)
		hints.ai_family = AF_INET;
	else if (type == DNSType::IPV6)
		hints.ai_family = AF_INET6;

	const char *servicep = service.empty() ? nullptr : service.c_str();

	*res = nullptr;
	int result = getaddrinfo(host.c_str(), servicep, &hints, res);
	if (result == EAI_AGAIN) {
		// Temporary failure.  Since this already blocks, let's just try once more.
		sleep_ms(1, "dns-resolve-poll");
		result = getaddrinfo(host.c_str(), servicep, &hints, res);
	}

	if (result != 0) {
#ifdef _WIN32
		error = ConvertWStringToUTF8(gai_strerror(result));
#else
		error = gai_strerror(result);
#endif
		if (*res != nullptr)
			freeaddrinfo(*res);
		*res = nullptr;
		return false;
	}

	return true;
}

void DNSResolveFree(addrinfo *res)
{
	if (res)
		freeaddrinfo(res);
}

bool GetIPList(std::vector<std::string> &IP4s) {
	char ipstr[INET6_ADDRSTRLEN]; // We use IPv6 length since it's longer than IPv4
// getifaddrs first appeared in glibc 2.3, On Android officially supported since __ANDROID_API__ >= 24
#if defined(_IFADDRS_H_) || (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3) || (__ANDROID_API__ >= 24)
	INFO_LOG(Log::sceNet, "GetIPList from getifaddrs");
	struct ifaddrs* ifAddrStruct = NULL;
	struct ifaddrs* ifa = NULL;

	getifaddrs(&ifAddrStruct);
	if (ifAddrStruct != NULL) {
		for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr) {
				continue;
			}
			if (ifa->ifa_addr->sa_family == AF_INET) {
				// is a valid IP4 Address
				if (inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ipstr, sizeof(ipstr)) != 0) {
					IP4s.push_back(ipstr);
				}
			}
			/*else if (ifa->ifa_addr->sa_family == AF_INET6) {
				// is a valid IP6 Address
				if (inet_ntop(AF_INET6, &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr, ipstr, sizeof(ipstr)) != 0) {
					IP6s.push_back(ipstr);
				}
			}*/
		}

		freeifaddrs(ifAddrStruct);
		return true;
	}
#elif defined(SIOCGIFCONF) // Better detection on Linux/UNIX/MacOS/some Android
	INFO_LOG(Log::sceNet, "GetIPList from SIOCGIFCONF");
	static struct ifreq ifreqs[32];
	struct ifconf ifc{};
	ifc.ifc_req = ifreqs;
	ifc.ifc_len = sizeof(ifreqs);

	int sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		ERROR_LOG(Log::sceNet, "GetIPList failed to create socket (result = %i, errno = %i)", sd, socket_errno);
		return false;
	}

	int r = ioctl(sd, SIOCGIFCONF, (char*)&ifc);
	if (r != 0) {
		ERROR_LOG(Log::sceNet, "GetIPList failed ioctl/SIOCGIFCONF (result = %i, errno = %i)", r, socket_errno);
		return false;
	}

	struct ifreq* item;
	struct sockaddr* addr;

	for (int i = 0; i < ifc.ifc_len / sizeof(struct ifreq); ++i)
	{
		item = &ifreqs[i];
		addr = &(item->ifr_addr);

		// Get the IP address
		r = ioctl(sd, SIOCGIFADDR, item);
		if (r != 0)
		{
			ERROR_LOG(Log::sceNet, "GetIPList failed ioctl/SIOCGIFADDR (i = %i, result = %i, errno = %i)", i, r, socket_errno);
		}

		if (ifreqs[i].ifr_addr.sa_family == AF_INET) {
			// is a valid IP4 Address
			if (inet_ntop(AF_INET, &((struct sockaddr_in*)addr)->sin_addr, ipstr, sizeof(ipstr)) != 0) {
				IP4s.emplace_back(ipstr);
			}
		}
		/*else if (ifreqs[i].ifr_addr.sa_family == AF_INET6) {
			// is a valid IP6 Address
			if (inet_ntop(AF_INET6, &((struct sockaddr_in6*)addr)->sin6_addr, ipstr, sizeof(ipstr)) != 0) {
				IP6s.push_back(ipstr);
			}
		}*/
	}

	close(sd);
	return true;
#else // Fallback to POSIX/Cross-platform way but may not works well on Linux (ie. only shows 127.0.0.1)
	INFO_LOG(Log::sceNet, "GetIPList from Fallback");
	struct addrinfo hints, * res, * p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
	hints.ai_socktype = SOCK_DGRAM;

	// Get local host name
	char szHostName[256] = "";
	if (::gethostname(szHostName, sizeof(szHostName))) {
		// Error handling
	}

	int status;
	if ((status = getaddrinfo(szHostName, NULL, &hints, &res)) != 0) {
		//fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return false;
	}
	for (p = res; p != NULL; p = p->ai_next) {
		if (p->ai_family == AF_INET) {
			// is a valid IP4 Address
			if (inet_ntop(p->ai_family, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), ipstr, sizeof(ipstr)) != 0) {
				IP4s.push_back(ipstr);
			}
		}
		/*else if (p->ai_family == AF_INET6) {
			// is a valid IP6 Address
			if (inet_ntop(p->ai_family, &(((struct sockaddr_in6*)p->ai_addr)->sin6_addr), ipstr, sizeof(ipstr)) != 0) {
				IP6s.push_back(ipstr);
			}
		}*/
	}

	freeaddrinfo(res); // free the linked list
	return true;
#endif
	return false;
}

// IP address parser
int inet_pton(int af, const char* src, void* dst)
{
	if (af == AF_INET)
	{
		unsigned char *ip = (unsigned char *)dst;
		int k = 0, x = 0;
		char ch;
		for (int i = 0; (ch = src[i]) != 0; i++)
		{
			if (ch == '.')
			{
				ip[k++] = x;
				if (k == 4)
					return 0;
				x = 0;
			}
			else if (ch < '0' || ch > '9')
				return 0;
			else
				x = x * 10 + ch - '0';
			if (x > 255)
				return 0;
		}
		ip[k++] = x;
		if (k != 4)
			return 0;
	}
	else if (af == AF_INET6)
	{
		unsigned short* ip = ( unsigned short* )dst;
		int i;
		for (i = 0; i < 8; i++) ip[i] = 0;
		int k = 0;
		unsigned int x = 0;
		char ch;
		int marknum = 0;
		for (i = 0; src[i] != 0; i++)
		{
			if (src[i] == ':')
				marknum++;
		}
		for (i = 0; (ch = src[i]) != 0; i++)
		{
			if (ch == ':')
			{
				x = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
				ip[k++] = x;
				if (k == 8)
					return 0;
				x = 0;
				if (i > 0 && src[i - 1] == ':')
					k += 7 - marknum;
			}
			else if (ch >= '0' && ch <= '9')
				x = x * 16 + ch - '0';
			else if (ch >= 'a' && ch <= 'f')
				x = x * 16 + ch - 'a' + 10;
			else if (ch >= 'A' && ch <= 'F')
				x = x * 16 + ch - 'A' + 10;
			else
				return 0;
			if (x > 0xFFFF)
				return 0;
		}
		x = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
		ip[k++] = x;
		if (k != 8)
			return 0;
	}
	return 1;
}

// Structs for implementing DNS are available here:
// https://web.archive.org/web/20201204080751/https://www.binarytides.com/dns-query-code-in-c-with-winsock/

#define DNS_PORT 53
#define DNS_QUERY_TYPE_A 1
#define DNS_QUERY_CLASS_IN 1

// DNS header structure
struct DNSHeader {
	uint16_t id;       // Identifier
	uint16_t flags;    // Flags
	uint16_t q_count;  // Number of questions
	uint16_t ans_count;  // Number of answers
	uint16_t auth_count; // Number of authority records
	uint16_t add_count;  // Number of additional records
};

// Function to convert a domain name to DNS query format
// http://www.tcpipguide.com/free/t_DNSNameNotationandMessageCompressionTechnique.htm
static void encode_domain_name(const char *domain, unsigned char *encoded) {
	const char *pos = domain;
	unsigned char *ptr = encoded;

	while (*pos) {
		const char *start = pos;
		while (*pos && *pos != '.') {
			pos++;
		}

		*ptr++ = (unsigned char)(pos - start);  // length field
		memcpy(ptr, start, pos - start);
		ptr += pos - start;

		if (*pos == '.') {
			pos++;
		}
	}
	*ptr = 0; // End of domain name
}

// Function to parse and print the DNS response
static bool parse_dns_response(unsigned char *buffer, size_t response_len, uint32_t *output) {
	DNSHeader *dns = (DNSHeader *)buffer;
	unsigned char *ptr = buffer + sizeof(struct DNSHeader);

	DEBUG_LOG(Log::sceNet, "DNS Response:");
	DEBUG_LOG(Log::sceNet, "ID: 0x%x", ntohs(dns->id));
	DEBUG_LOG(Log::sceNet, "Flags: 0x%x", ntohs(dns->flags));
	DEBUG_LOG(Log::sceNet, "Questions: %d", ntohs(dns->q_count));
	DEBUG_LOG(Log::sceNet, "Answers: %d", ntohs(dns->ans_count));
	DEBUG_LOG(Log::sceNet, "Authority Records: %d", ntohs(dns->auth_count));
	DEBUG_LOG(Log::sceNet, "Additional Records: %d", ntohs(dns->add_count));

	// Skip over the question section
	const int q_count = ntohs(dns->q_count);
	for (int i = 0; i < q_count; i++) {
		while (*ptr != 0) {
			ptr += (*ptr) + 1;
		}
		ptr += 5; // Null byte + QTYPE (2 bytes) + QCLASS (2 bytes)
	}

	*output = 0;

	// Parse the answer section
	const int ans_count = ntohs(dns->ans_count);
	for (int i = 0; i < ans_count; i++) {
		DEBUG_LOG(Log::sceNet, "Answer %d:\n", i + 1);

		// Skip the name (can be a pointer or a sequence)
		if ((*ptr & 0xC0) == 0xC0) {
			ptr += 2; // Pointer (2 bytes)
		} else {
			while (*ptr != 0) ptr += (*ptr) + 1;
			ptr++;
		}

		// TODO: Use a struct or something.
		uint16_t type = ntohs(*((uint16_t *)ptr));
		ptr += 2;
		uint16_t clazz = ntohs(*((uint16_t *)ptr));
		ptr += 2;
		uint32_t ttl = ntohl(*((uint32_t *)ptr));
		ptr += 4;
		uint16_t data_len = ntohs(*((uint16_t *)ptr));
		ptr += 2;

		DEBUG_LOG(Log::sceNet, "  Type: %d", type);
		DEBUG_LOG(Log::sceNet, "  Class: %d", clazz);
		DEBUG_LOG(Log::sceNet, "  TTL: %u", ttl);
		DEBUG_LOG(Log::sceNet, "  Data length: %d", (int)data_len);

		if (type == DNS_QUERY_TYPE_A && data_len == 4) {
			// IPv4 address
			char ip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, ptr, ip, sizeof(ip));
			DEBUG_LOG(Log::sceNet, "  IPV4 Address: %s", ip);
			memcpy(output, ptr, 4);
			// Skipping further responses.
			return true;
		}

		ptr += data_len;
	}
	return false;
}

// This was written by ChatGPT! (And then cleaned up...)
bool DirectDNSLookupIPV4(const char *dns_server_ip, const char *domain, uint32_t *ipv4_addr) {
	if (!strlen(dns_server_ip)) {
		WARN_LOG(Log::sceNet, "Direct lookup: DNS server not specified");
		return false;
	}

	if (!strlen(domain)) {
		ERROR_LOG(Log::sceNet, "Direct lookup: Can't look up an empty domain");
		return false;
	}

	SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	// Create UDP socket
	if (sockfd < 0) {
		ERROR_LOG(Log::sceNet, "Socket creation for direct DNS failed");
		return 1;
	}

	struct sockaddr_in server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(DNS_PORT);

	if (net::inet_pton(AF_INET, dns_server_ip, &server_addr.sin_addr) <= 0) {
		ERROR_LOG(Log::sceNet,"Invalid DNS server IP address %s", dns_server_ip);
		closesocket(sockfd);
		return 1;
	}

	// Build DNS query
	unsigned char buffer[1024]{};
	struct DNSHeader *dns = (struct DNSHeader *)buffer;
	dns->id = htons(0x1234);  // Random ID
	dns->flags = htons(0x0100); // Standard query
	dns->q_count = htons(1);    // One question

	unsigned char *qname = buffer + sizeof(DNSHeader);
	encode_domain_name(domain, qname);

	unsigned char *qinfo = qname + strlen((const char *)qname) + 1;
	*((uint16_t *)qinfo) = htons(DNS_QUERY_TYPE_A); // Query type: A
	*((uint16_t *)(qinfo + 2)) = htons(DNS_QUERY_CLASS_IN); // Query class: IN

	// Send DNS query
	size_t query_len = sizeof(DNSHeader) + (qinfo - buffer) + 4;
	if (sendto(sockfd, (const char *)buffer, (int)query_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		ERROR_LOG(Log::sceNet, "Failed to send DNS query");
		closesocket(sockfd);
		return 1;
	}

	// Receive DNS response
	socklen_t server_len = sizeof(server_addr);
	size_t response_len;
	if ((response_len = recvfrom(sockfd, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &server_len)) < 0) {
		ERROR_LOG(Log::sceNet, "Failed to receive DNS response");
		closesocket(sockfd);
		return 1;
	}
	// Close socket
	closesocket(sockfd);

	// Done communicating, time to parse.
	return parse_dns_response(buffer, response_len, ipv4_addr);
}

}  // namespace net
