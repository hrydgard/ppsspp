#ifndef _NET_HTTP_HTTP_HEADERS
#define _NET_HTTP_HTTP_HEADERS

#include <string>
#include <unordered_map>
#include "base/buffer.h"

namespace net {
class InputSink;
};

namespace http {

class RequestHeader {
 public:
  RequestHeader();
  ~RequestHeader();
  // Public variables since it doesn't make sense
  // to bother with accessors for all these.
  int status;
  // Intentional misspelling.
  char *referer;
  char *user_agent;
  char *resource;
  char *params;
  int content_length;
  std::unordered_map<std::string, std::string> other;
  enum RequestType {
    SIMPLE, FULL,
  };
  RequestType type;
  enum Method {
    GET,
    HEAD,
    POST,
    UNSUPPORTED,
  };
  Method method;
  bool ok;
  void ParseHeaders(net::InputSink *sink);
  bool GetParamValue(const char *param_name, std::string *value) const;
  bool GetOther(const char *name, std::string *value) const;
 private:
  int ParseHttpHeader(const char *buffer);
  bool first_header_;
  
  DISALLOW_COPY_AND_ASSIGN(RequestHeader);
};

}  // namespace http

#endif
