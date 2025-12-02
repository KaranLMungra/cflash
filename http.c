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
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define _HTTP_PARSE_INC(i, j)                                                  \
  i++;                                                                         \
  j = i;

#define _HTTP_PARSE_INC_2(i, j)                                                \
  i += 2;                                                                      \
  j = i;

static int _skip_until_space(int curr_pos, const char *buffer, int buf_len) {
  size_t remaining = (size_t)(buf_len - curr_pos);
  const char *ptr = memchr(buffer + curr_pos, ' ', remaining);
  if (ptr != NULL) {
    return (int)(ptr - buffer);
  }
  return HTTP_INVALID;
}

static int _skip_until_eol(int curr_pos, const char *buffer, int buf_len) {
  size_t remaining = (size_t)(buf_len - curr_pos - 1);
  const char *r_pos = memchr(buffer + curr_pos, '\r', remaining);
  if (r_pos == NULL) {
    return HTTP_INVALID;
  }

  int pos = (int)(r_pos - buffer);
  if (buffer[pos + 1] == '\n') {
    return pos;
  }
  return HTTP_INVALID;
}

static enum HttpMethod _is_valid_http_method(const char *buffer, int until) {
  for (int i = 0; i < HTTP_METHOD_NUM; ++i) {
    if (strncmp(HTTP_METHODS_STR[i], buffer, (size_t)until) == 0) {
      return i;
    }
  }
  return HTTP_INVALID;
}

static int _is_supported_http_version(const char *buffer, int until) {
  if (until != (int)strlen(HTTP_SUPPORTED_VERSION)) {
    return HTTP_INVALID;
  }
  if (strncmp(HTTP_SUPPORTED_VERSION, buffer, (size_t)until) == 0) {
    return 0;
  }
  return HTTP_INVALID;
}

// Path is stored as a slice into the main buffer.
static int _extract_path(const char *buffer, int until,
                         struct HttpRequest *req) {
  if (until <= 0 || until > HTTP_MAX_PATH_LENGTH) {
    return HTTP_INVALID;
  }
  req->path = buffer;
  req->path_length = (size_t)until;
  return 0;
}

static int _extract_http_header(const char *buffer, int until,
                                struct HttpRequest *req) {
  const char *colon_ptr = memchr(buffer, ':', (size_t)until);
  if (!colon_ptr) {
    return HTTP_INVALID;
  }

  int key_end = (int)(colon_ptr - buffer);
  int curr_header = (int)req->num_headers++;
  if (curr_header >= MAX_HEADERS) {
    return HTTP_INVALID;
  }

  int name_len = key_end;
  if (name_len <= 0 || name_len > HTTP_HEADER_NAME_MAX) {
    return HTTP_INVALID;
  }

  int value_start = key_end + 2;
  int value_len = until - value_start;
  if (value_len < 0 || value_len > HTTP_HEADER_VALUE_MAX) {
    return HTTP_INVALID;
  }

  req->headers[curr_header].name = buffer;
  req->headers[curr_header].name_length = (size_t)name_len;
  req->headers[curr_header].value = buffer + value_start;
  req->headers[curr_header].value_length = (size_t)value_len;

  return 0;
}

void init_http_request(struct HttpRequest *req) {
  req->cd = 0;
  req->method = HTTP_GET;
  req->path = NULL;
  req->path_length = 0;
  req->num_headers = 0;
  req->headers = calloc(MAX_HEADERS, sizeof(struct HttpHeader));
  req->body = NULL;
  req->body_length = 0;
  req->buffer_length = 0;
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
  req->method = (enum HttpMethod)method;

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
    req->num_headers = 0;
    req->body = NULL;
    req->body_length = 0;
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

  // TODO: Instead this should be done based on content length
  _HTTP_PARSE_INC_2(i, j);
  if (i < 0 || i > buf_len) {
    return HTTP_BAD_REQUEST;
  }

  req->body = buffer + i;
  req->body_length = (size_t)(buf_len - i);

  return HTTP_OK;
}

void print_http_request(const struct HttpRequest *req) {
  printf("%.*s '%.*s' %s\r\n", (int)req->path_length,
         req->path ? req->path : "", (int)req->path_length,
         req->path ? req->path : "", HTTP_SUPPORTED_VERSION);

  for (size_t i = 0; i < req->num_headers; ++i) {
    const struct HttpHeader *h = &req->headers[i];
    printf("%.*s: %.*s\r\n", (int)h->name_length, h->name ? h->name : "",
           (int)h->value_length, h->value ? h->value : "");
  }
  printf("\r\n");

  if (req->body_length > 0 && req->body) {
    printf("%.*s", (int)req->body_length, req->body);
  }
}

void http_free_request(struct HttpRequest *req) {
  free(req->headers);
  req->headers = NULL;
}

int begin_http_server(struct HttpServer *server, const char* server_host, int server_port) {
  if (server == NULL) {
    return -1;
  }

  int err = 0;
  int sd = -1;
  int optval = 1;
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr = {
          .s_addr = inet_addr(server_host)
      },
      .sin_port = htons(server_port)
  };

  sd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (sd < 0) {
    fprintf(stderr, "ERROR: unable to create socket, due to error %s\n",
            strerror(errno));
    return -1;
  }

  err = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  if (err < 0) {
    fprintf(stderr, "ERROR: unable to set option SO_REUSERADDR on socket, due to error %s\n",
            strerror(errno));
    return -1;
  }

  err = bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
  if (err < 0) {
    fprintf(stderr, "ERROR: unable to bind %s:%d to socket, due to error %s\n", server_host, server_port, strerror(errno));
    return -1;
  }
  printf("INFO: starting server at %s:%d\n", server_host, server_port);
  server->sd = sd;
  memcpy(&server->addr, &addr, sizeof(struct sockaddr_in));
  return 0;
}

void end_http_server(struct HttpServer *server) {
  if (close(server->sd) < 0) {
    fprintf(stderr, "ERROR: unable to close socket, due to error %s\n",
            strerror(errno));
    return;
  }
}
