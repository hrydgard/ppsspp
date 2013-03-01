	#include "net/resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>	// gethostbyname
#include <sys/socket.h>
#else
#include <WinSock2.h>
#include <Ws2tcpip.h>
#undef min
#undef max
#endif


namespace net {

void Init()
{
#ifdef _WIN32
	WSADATA wsaData = {0};
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void Shutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

char *DNSResolveTry(const char *host, const char **err)
{
	struct hostent *hent;
	if((hent = gethostbyname(host)) == NULL)
	{
		*err = "Can't get IP";
		return NULL;
	}
	int iplen = 15; //XXX.XXX.XXX.XXX
	char *ip = (char *)malloc(iplen+1);
	memset(ip, 0, iplen+1);
	if(inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip, iplen) == NULL)
	{
		*err = "Can't resolve host";
		return NULL;
	}
	return ip;
}

char *DNSResolve(const char *host)
{
	const char *err;
	char *ip = DNSResolveTry(host, &err);
	if (ip == NULL)
	{
		perror(err);
		exit(1);
	}
	return ip;
}

}
