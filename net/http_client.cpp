#include "net/http_client.h"

// for inet_pton
#define _WIN32_WINNT 0x600

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/buffer.h"
#include "base/stringutil.h"
#include "net/resolve.h"
// #include "strings/strutil.h"

namespace net {

Connection::Connection() 
    : port_(-1), sock_(-1) {
}

Connection::~Connection() {
  Disconnect();
}

bool Connection::Resolve(const char *host, int port) {
  CHECK_EQ(-1, sock_);
  host_ = host;
  port_ = port;

  const char *ip = net::DNSResolve(host);
  // VLOG(1) << "Resolved " << host << " to " << ip;
  remote_.sin_family = AF_INET;
  int tmpres = inet_pton(AF_INET, ip, (void *)(&(remote_.sin_addr.s_addr)));
  CHECK_GE(tmpres, 0);  // << "inet_pton failed";
  CHECK_NE(0, tmpres);  // << ip << " not a valid IP address";
  remote_.sin_port = htons(port);
  return true;
}

void Connection::Connect() {
  CHECK_GE(port_, 0);
  sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CHECK_GE(sock_, 0);
  //VLOG(1) << "Connecting to " << host_ << ":" << port_;

  // poll once per second.. should find a way to do this blocking.
  int retval = -1;
  while (retval < 0) {
    retval = connect(sock_, (sockaddr *)&remote_, sizeof(struct sockaddr));
    if (retval >= 0) break;
#ifdef _WIN32
    Sleep(1);
#else
    sleep(1);
#endif
  }
}

void Connection::Disconnect() {
  if (sock_ != -1) {
    closesocket(sock_);
    sock_ = -1;
  } else {
    WLOG("Socket was already disconnected.");
  }
}

void Connection::Reconnect() {
  Disconnect();
  Connect();
}

}  // net

namespace http {

Client::Client() {
}
Client::~Client() {
}

#define USERAGENT "METAGET 1.0"

void Client::GET(const char *resource, Buffer *output) {
  Buffer buffer;
  const char *tpl = "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n";
  buffer.Printf(tpl, resource, host_.c_str());
  CHECK(buffer.FlushSocket(sock()));

  // Snarf all the data we can.
  output->ReadAll(sock());

  // Skip the header.
  while (output->SkipLineCRLF() > 0)
    ;

  // output now contains the rest of the reply.
}

int Client::POST(const char *resource, const std::string &data, Buffer *output) {
  Buffer buffer;
  const char *tpl = "POST %s HTTP/1.0\r\nContent-Length: %d\r\n\r\n";
  buffer.Printf(tpl, resource, (int)data.size());
  buffer.Append(data);
  CHECK(buffer.Flush(sock()));

  // I guess we could add a deadline here.
  output->ReadAll(sock());

  if (output->size() == 0) {
    // The connection was closed.
    ELOG("POST failed.");
    return -1;
  }

  std::string debug_data;
  output->PeekAll(&debug_data);
  
  //VLOG(1) << "Reply size (before stripping headers): " << debug_data.size();
  std::string debug_str;
  StringToHexString(debug_data, &debug_str);
  // Tear off the http headers, leaving the actual response data.
  std::string firstline;
  CHECK_GT(output->TakeLineCRLF(&firstline), 0);
  int code = atoi(&firstline[9]);  // ugggly hardcoding
  //VLOG(1) << "HTTP result code: " << code;
  while (true) {
    int skipped = output->SkipLineCRLF();
    if (skipped == 0)
      break;
  }
  output->PeekAll(&debug_data);
  return code;
}

}  // http
