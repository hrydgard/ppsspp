#include "base/timeutil.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

#else

#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <sys/wait.h>         /*  for waitpid()             */
#include <netinet/in.h>       /*  struct sockaddr_in        */
#include <arpa/inet.h>        /*  inet (3) funtions         */
#include <unistd.h>           /*  misc. UNIX functions      */

#endif

#include <stdio.h>
#include <stdlib.h>

#include "base/functional.h"
#include "base/logging.h"
#include "base/buffer.h"
#include "file/fd_util.h"
#include "net/http_server.h"
#include "thread/executor.h"

namespace http {

Request::Request(int fd)
    : fd_(fd) {
  in_buffer_ = new Buffer;
  out_buffer_ = new Buffer;
  header_.ParseHeaders(fd_);

  if (header_.ok) {
    // Read the rest, too.
    if (header_.content_length >= 0) {
      in_buffer_->Read(fd_, header_.content_length);
    }
    ILOG("The request carried with it %i bytes", (int)in_buffer_->size());
  } else {
    Close();
  }
}

Request::~Request() {
  Close();

  CHECK(in_buffer_->empty());
  delete in_buffer_;
  CHECK(out_buffer_->empty());
  delete out_buffer_;
}

void Request::WriteHttpResponseHeader(int status, int size) const {
  Buffer *buffer = out_buffer_;
  buffer->Printf("HTTP/1.0 %d OK\r\n", status);
  buffer->Append("Server: SuperDuperServer v0.1\r\n");
  buffer->Append("Content-Type: text/html\r\n");
  if (size >= 0) {
    buffer->Printf("Content-Length: %i\r\n", size);
  }
  buffer->Append("\r\n");
}

void Request::WritePartial() const {
  CHECK(fd_);
  out_buffer_->Flush(fd_);
}

void Request::Write() {
  CHECK(fd_);
  WritePartial();
  Close();
}

void Request::Close() {
  if (fd_) {
    close(fd_);
    fd_ = 0;
  }
}

Server::Server(threading::Executor *executor) 
  : port_(0), executor_(executor) {
  RegisterHandler("/", std::bind(&Server::HandleListing, this, placeholder::_1));
}

void Server::RegisterHandler(const char *url_path, UrlHandlerFunc handler) {
  handlers_[std::string(url_path)] = handler;
}

bool Server::Run(int port) {
  ILOG("HTTP server started on port %i", port);
  port_ = port;

  int listener = socket(AF_INET, SOCK_STREAM, 0);
  CHECK_GE(listener, 0);

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  int opt = 1;
  // Enable re-binding to avoid the pain when restarting the server quickly.
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

  if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ELOG("Failed to bind to port %i. Bailing.", port);
    return false;
  }

  // 1024 is the max number of queued requests.
  CHECK_GE(listen(listener, 1024), 0);
  while (true) {
    sockaddr client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    int conn_fd = accept(listener, &client_addr, &client_addr_size);
    if (conn_fd >= 0) {
      executor_->Run(std::bind(&Server::HandleConnection, this, conn_fd));
    } else {
			FLOG("socket accept failed: %i", conn_fd);
    }
  }

  // We'll never get here. Ever.
  return true;
}

void Server::HandleConnection(int conn_fd) {
  Request request(conn_fd);
  if (!request.IsOK()) {
    WLOG("Bad request, ignoring.");
    return;
  }
  HandleRequestDefault(request);
  request.WritePartial();
}

void Server::HandleRequest(const Request &request) {
  HandleRequestDefault(request);
}

void Server::HandleRequestDefault(const Request &request) {
  // First, look through all handlers. If we got one, use it.
  for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
    if (iter->first == request.resource()) {
      (iter->second)(request);
      return;
    }
  }
  ILOG("No handler for '%s', falling back to 404.", request.resource());
  const char *payload = "<html><body>404 not found</body></html>\r\n";
  request.WriteHttpResponseHeader(404, (int)strlen(payload));
  request.out_buffer()->Append(payload);
}

void Server::HandleListing(const Request &request) {
  for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
    request.out_buffer()->Printf("%s", iter->first.c_str());
  }
}

}  // namespace http
