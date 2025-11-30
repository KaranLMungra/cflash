/*
 * Echo Server
 * Http Server
 * POST /echo HTTP/1.1
 * Host: http://localhost:8000/echo
 * Content-Type: text/plain
 * Content-Length: 13
 *
 * Hello, World!
 */

#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _HTTP_PARSE_INC(i, j)                                                  \
  i++;                                                                         \
  j = i;
#define _HTTP_PARSE_INC_2(i, j)                                                \
  i += 2;                                                                      \
  j = i;

int _skip_until_space(int curr_pos, const char *buffer, int buf_len) {
  size_t remaining = (size_t)(buf_len - curr_pos);
  const char *colon_ptr = memchr(buffer + curr_pos, ' ', remaining);
  if (colon_ptr != NULL) {
    return (int)(colon_ptr - buffer);
  }
  return HTTP_INVALID;
}
int _skip_until_colon(int curr_pos, const char *buffer, int buf_len) {
  size_t remaining = (size_t)(buf_len - curr_pos);
  const char *colon_ptr = memchr(buffer + curr_pos, ':', remaining);
  if (colon_ptr != NULL) {
    return (int)(colon_ptr - buffer);
  }
  return HTTP_INVALID;
}

int _skip_until_eol(int curr_pos, const char *buffer, int buf_len) {
  size_t remaining = (size_t)(buf_len - curr_pos - 1);
  const char *r_pos = memchr(buffer + curr_pos, '\r', remaining);
  if (r_pos == NULL) {
    return HTTP_INVALID;
  }

  int pos = r_pos - buffer;
  if (buffer[pos + 1] == '\n') {
    return pos;
  }
  return HTTP_INVALID;
}

enum HttpMethod _is_valid_http_method(const char *buffer, int until) {
  for (int i = 0; i < HTTP_METHOD_NUM; ++i) {
    if (strncmp(HTTP_METHODS_STR[i], buffer, until) == 0) {
      return i;
    }
  }
  return HTTP_INVALID;
}

int _is_supported_http_version(const char *buffer, int until) {
  if (until != strlen(HTTP_SUPPORTED_VERSION)) {
    return HTTP_INVALID;
  }
  if (strncmp(HTTP_SUPPORTED_VERSION, buffer, until) == 0) {
    return 0;
  }
  return HTTP_INVALID;
}

int _extract_path(const char *buffer, int until, struct HttpRequest *req) {
  if (until > HTTP_MAX_BUFFER_SIZE) {
    // TODO: Might need allocations
    return HTTP_INVALID;
  }
  memcpy(req->path, buffer, until);
  req->path_length = until;
  return 0;
}

int _extract_http_header(const char *buffer, int until,
                         struct HttpRequest *req) {
  int key_end = _skip_until_colon(0, buffer, until);
  if (key_end == HTTP_INVALID) {
    return HTTP_INVALID;
  }

  int curr_header = req->num_headers++;

  int name_len = key_end;
  if (name_len >= HTTP_MAX_BUFFER_SIZE) { // whatever max you defined
    return HTTP_INVALID;
  }
  memcpy(req->headers[curr_header].name, buffer, name_len);
  req->headers[curr_header].name_length = name_len;

  int value_start = key_end + 2;
  int value_len = until - value_start;
  if (value_len < 0 || value_len >= HTTP_MAX_BUFFER_SIZE) {
    return HTTP_INVALID;
  }
  memcpy(req->headers[curr_header].value, buffer + value_start, value_len);
  req->headers[curr_header].value_length = value_len;

  return 0;
}

void init_http_request(struct HttpRequest *req) {
  req->cd = 0;
  req->body_length = 0;
  req->num_headers = 0;
  req->headers = calloc(MAX_HEADERS, sizeof(struct HttpHeader));
}

enum HttpStatus make_http_request(const char *buffer, int buf_len,
                                  struct HttpRequest *req) {
  int i = 0;
  int j = 0;
  i = _skip_until_space(i, buffer, buf_len);
  if (i == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  int method = _is_valid_http_method(buffer, i);
  if (method == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  req->method = method;
  _HTTP_PARSE_INC(i, j);
  i = _skip_until_space(i, buffer, buf_len);
  if (i == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  if (_extract_path(buffer + j, i - j, req) == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  _HTTP_PARSE_INC(i, j);
  i = _skip_until_eol(i, buffer, buf_len);
  if (i == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  if (_is_supported_http_version(buffer + j, i - j) == HTTP_INVALID) {
    return HTTP_VERSION_NOT_SUPPORTED;
  }
  _HTTP_PARSE_INC_2(i, j);
  i = _skip_until_eol(i, buffer, buf_len);
  if (i == HTTP_INVALID) {
    return HTTP_BAD_REQUEST;
  }
  if (i - j == 0) {
    return HTTP_OK;
  }
  req->num_headers = 0;
  while (_extract_http_header(buffer + j, i - j, req) != HTTP_INVALID) {
    _HTTP_PARSE_INC_2(i, j);
    i = _skip_until_eol(i, buffer, buf_len);
    if (i == HTTP_INVALID) {
      return HTTP_BAD_REQUEST;
    }
    if (i - j == 0) {
      break;
    }
  }
  // Read Body
  _HTTP_PARSE_INC_2(i, j);
  // for (int k = 0; k < req->num_headers; ++k) {
  //   if (strcmp(req->headers[k].name, "content-length") == 0) {
  //     req->body_length = atoi(req->headers[k].value);
  //     memcpy(req->body, buffer + i, req->body_length);
  //   }
  // }
  // TODO: We are ignoring this header for now, as we are optimizing, need to include later
  req->body_length = buf_len - i;
  memcpy(req->body, buffer+i, req->body_length);
  return HTTP_OK;
}

void print_http_request(const struct HttpRequest *req) {
  printf("%s '%s' %s\r\n", HTTP_METHODS_STR[req->method], req->path,
         HTTP_SUPPORTED_VERSION);
  for (int i = 0; i < req->num_headers; ++i) {
    printf("%s: %s\r\n", req->headers[i].name, req->headers[i].value);
  }
  printf("\r\n");
  if (req->body_length > 0) {
    printf("%s", req->body);
  }
}

void http_free_request(struct HttpRequest *req) { free(req->headers); }
