#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include <map>

#include "base/functional.h"
#include "base/buffer.h"
#include "net/http_headers.h"
#include "thread/executor.h"

namespace http {

class Request {
 public:
  Request(int fd);
  ~Request();

  const char *resource() const {
    return header_.resource;
  }

  bool GetParamValue(const char *param_name, std::string *value) const {
    return header_.GetParamValue(param_name, value);
  }

  Buffer *in_buffer() const { return in_buffer_; }
  Buffer *out_buffer() const { return out_buffer_; }

  // TODO: Remove, in favor of PartialWrite and friends.
  int fd() const { return fd_; }

  void WritePartial() const;
  void Write();
  void Close();

  bool IsOK() const { return fd_ > 0; }

  // If size is negative, no Content-Length: line is written.
  void WriteHttpResponseHeader(int status, int size = -1) const;

 private:
  Buffer *in_buffer_;
  Buffer *out_buffer_;
  RequestHeader header_;
  int fd_;
};

// Register handlers on this class to serve stuff.
class Server {
 public:
  Server(threading::Executor *executor);

  typedef std::function<void(const Request &)> UrlHandlerFunc;
  typedef std::map<std::string, UrlHandlerFunc> UrlHandlerMap;

  // Runs forever, serving request. If you want to do something else than serve pages,
  // better put this on a thread. Returns false if failed to start serving, never
  // returns if successful.
  bool Run(int port);

  void RegisterHandler(const char *url_path, UrlHandlerFunc handler);

  // If you want to customize things at a lower level than just a simple path handler,
  // then inherit and override this. Implementations should forward to HandleRequestDefault
  // if they don't recognize the url.
  virtual void HandleRequest(const Request &request);

 private:
  void HandleConnection(int conn_fd);

  void GetRequest(Request *request);

  // Things like default 404, etc.
  void HandleRequestDefault(const Request &request);
  
  // Neat built-in handlers that are tied to the server.
  void HandleListing(const Request &request);

  int port_;

  UrlHandlerMap handlers_;

  threading::Executor *executor_;
};

}  // namespace http

#endif  // _HTTP_SERVER_H
