#include "net/http_headers.h"

#include <stdio.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/stringutil.h"
#include "file/fd_util.h"

namespace http {

RequestHeader::RequestHeader()
    : status(200), referer(0), user_agent(0),
      resource(0), params(0), content_length(-1), first_header_(true) {
}

RequestHeader::~RequestHeader() {
  delete [] referer;
  delete [] user_agent;
  delete [] resource;
  delete [] params;
}

bool RequestHeader::GetParamValue(const char *param_name, std::string *value) const {
  if (!params)
    return false;
  std::string p(params);
  std::vector<std::string> v;
	SplitString(p, '&', v);
  for (size_t i = 0; i < v.size(); i++) {
    std::vector<std::string> parts;
		SplitString(v[i], '=', parts);
    ILOG("Param: %s Value: %s", parts[0].c_str(), parts[1].c_str());
    if (parts[0] == param_name) {
      *value = parts[1];
      return true;
    }
  }
  return false;
}

// Intended to be a mad fast parser. It's not THAT fast currently, there's still
// things to optimize, but meh.
int RequestHeader::ParseHttpHeader(const char *buffer) {
  if (first_header_) {
    // Step 1: Method
    first_header_ = false;
    if (!memcmp(buffer, "GET ", 4)) {
      method = GET;
      buffer += 4;
    } else if (!memcmp(buffer, "HEAD ", 5)) {
      method = HEAD;
      buffer += 5;
    } else if (!memcmp(buffer, "POST ", 5)) {
      method = POST;
      buffer += 5;
    } else {
      method = UNSUPPORTED;
      status = 501;
      return -1;
    }
    SkipSpace(&buffer);

    // Step 2: Resource, params (what's after the ?, if any)
    const char *endptr = strchr(buffer, ' ');
    const char *q_ptr = strchr(buffer, '?');

    int resource_name_len;
    if (q_ptr)
      resource_name_len = q_ptr - buffer;
    else
      resource_name_len = endptr - buffer;
    if (!resource_name_len) {
      status = 400;
      return -1;
    }
    resource = new char[resource_name_len + 1];
    memcpy(resource, buffer, resource_name_len);
    resource[resource_name_len] = '\0';
    if (q_ptr) {
      int param_length = endptr - q_ptr - 1;
      params = new char[param_length + 1];
      memcpy(params, q_ptr + 1, param_length);
      params[param_length] = '\0';
    }
    if (strstr(buffer, "HTTP/"))
      type = FULL;
    else
      type = SIMPLE;
    return 0;
  }

  // We have a real header to parse.
  const char *colon = strchr(buffer, ':');
  if (!colon) {
    status = 400;
    return -1;
  }

  // The header is formatted as key: value.
  int key_len = colon - buffer;
  char *key = new char[key_len + 1];
  strncpy(key, buffer, key_len);
  key[key_len] = 0;
  StringUpper(key, key_len);

  // Go to after the colon to get the value.
  buffer = colon + 1;
  SkipSpace(&buffer);
  int value_len = (int)strlen(buffer);
  
  if (!strcmp(key, "USER-AGENT")) {
    user_agent = new char[value_len + 1];
    memcpy(user_agent, buffer, value_len + 1);
    ILOG("user-agent: %s", user_agent);
  } else if (!strcmp(key, "REFERER")) {
    referer = new char[value_len + 1];
    memcpy(referer, buffer, value_len + 1);
  } else if (!strcmp(key, "CONTENT-LENGTH")) {
    content_length = atoi(buffer);
    ILOG("Content-Length: %i", (int)content_length);
  }

  delete [] key;
  return 0;
}

void RequestHeader::ParseHeaders(int fd) {
  int line_count = 0;
  // Loop through request headers.
  while (true) {
    if (!fd_util::WaitUntilReady(fd, 5.0)) {  // Wait max 5 secs.
      // Timed out or error.
      ok = false;
      return;
    }
    char buffer[1024];
    fd_util::ReadLine(fd, buffer, 1023);
    StringTrimEndNonAlphaNum(buffer);
    if (buffer[0] == '\0')
      break;
    ParseHttpHeader(buffer);
    line_count++;
    if (type == SIMPLE) {
      // Done!
      ILOG("Simple: Done parsing http request.");
      break;
    }
  }
  ILOG("finished parsing request.");
  ok = line_count > 1;
}

}  // namespace http
