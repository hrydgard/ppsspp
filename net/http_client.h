#ifndef _NET_HTTP_HTTP_CLIENT
#define _NET_HTTP_HTTP_CLIENT

#include "base/basictypes.h"
#include "base/buffer.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

namespace net {

class Connection {
 public:
  Connection();
  virtual ~Connection();

  // Inits the sockaddr_in.
  bool Resolve(const char *host, int port);

  void Connect();
  void Disconnect();

  // Disconnects, and connects. Doesn't re-resolve.
  void Reconnect();

  // Only to be used for bring-up and debugging.
  uintptr_t sock() const { return sock_; }

 protected:
  // Store the remote host here, so we can send it along through HTTP/1.1 requests.
  // TODO: Move to http::client?
  std::string host_;
  int port_;
  
  sockaddr_in remote_;

 private:
  uintptr_t sock_;

};

}  // namespace net

namespace http {

class Client : public net::Connection {
 public:
  Client();
  ~Client();

  void GET(const char *resource, Buffer *output);

  // Return value is the HTTP return code.
  int POST(const char *resource, const std::string &data, Buffer *output);

  // HEAD, PUT, DELETE aren't implemented yet.
};

}  // http

#endif  // _NET_HTTP_HTTP_CLIENT

