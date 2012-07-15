#include "net/resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>  // gethostbyname
#else
#include <WinSock2.h>
#include <Ws2tcpip.h>
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

char *DNSResolve(const char *host)
{
  struct hostent *hent;
  if((hent = gethostbyname(host)) == NULL)
  {
    perror("Can't get IP");
    exit(1);
  }
  int iplen = 15; //XXX.XXX.XXX.XXX
  char *ip = (char *)malloc(iplen+1);
  memset(ip, 0, iplen+1);
  if(inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip, iplen) == NULL)
  {
    perror("Can't resolve host");
    exit(1);
  }
  return ip;
}

}
