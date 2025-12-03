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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define _HTTP_PARSE_INC(i, j)                                                  \
  i++;                                                                         \
  j = i;

#define _HTTP_PARSE_INC_2(i, j)                                                \
  i += 2;                                                                      \
  j = i;

static volatile sig_atomic_t g_stop = 0;
#define NUM_EPOLL_EVENTS 10
#define NUM_IOVEC 10

static struct epoll_event events[NUM_EPOLL_EVENTS] = {0};
static struct iovec vec[NUM_IOVEC];

static inline char *_u64_to_str(char *end, size_t v) {
  // write digits backwards
  char *p = end;
  do {
    *--p = (char)('0' + (v % 10));
    v /= 10;
  } while (v);
  return p;
}

void _signal_handler(int signal, siginfo_t *info, void *context) {
  (void)signal;
  (void)info;
  (void)context;
  printf("gracefully  shutdown signal recieved\n");
  g_stop = 1;
}

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

int begin_http_server(struct HttpServer *server, const char *server_host,
                      int server_port, size_t max_concurrent_http_requests,
                      HttpHandler handler) {
  if (server == NULL) {
    return -1;
  }

  int sd = -1;
  int optval = 1;
  int epoll_fd = -1;
  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_addr = {.s_addr = inet_addr(server_host)},
                             .sin_port = htons(server_port)};
  struct sigaction act = {0};
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = &_signal_handler;

  struct HttpRequest *requests =
      calloc(max_concurrent_http_requests, sizeof(struct HttpRequest));
  for (int i = 0; i < max_concurrent_http_requests; ++i) {
    init_http_request(&requests[i]);
  }

  if (sigaction(SIGINT, &act, NULL) < 0) {
    fprintf(stderr, "unable to register signal handler, due to error %s\n",
            strerror(errno));
  }

  epoll_fd = epoll_create1(0);

  if (epoll_fd < 0) {
    fprintf(stderr, "ERROR: unable to create epoll, due to error %s\n",
            strerror(errno));
    return -1;
  }

  sd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (sd < 0) {
    fprintf(stderr, "ERROR: unable to create socket, due to error %s\n",
            strerror(errno));
    return -1;
  }
  if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
    fprintf(stderr,
            "ERROR: unable to set option SO_REUSERADDR on socket, due to error "
            "%s\n",
            strerror(errno));
    return -1;
  }

  if (bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
    fprintf(stderr, "ERROR: unable to bind %s:%d to socket, due to error %s\n",
            server_host, server_port, strerror(errno));
    return -1;
  }

  printf("INFO: starting server at %s:%d\n", server_host, server_port);
  server->sd = sd;
  server->epoll_fd = epoll_fd;
  memcpy(&server->addr, &addr, sizeof(struct sockaddr_in));
  server->requests = requests;
  server->max_concurrent_http_requests = max_concurrent_http_requests;
  server->handler = handler;
  return 0;
}

int run_http_server(struct HttpServer *server) {

  if (listen(server->sd, SOMAXCONN) < 0) {
    fprintf(stderr, "ERROR: unable to lisen socket, due to error %s\n",
            strerror(errno));
    return -1;
  }
  printf("INFO: listening for incoming connections...\n");

  struct sockaddr_in in_addr;
  socklen_t in_addr_len = (socklen_t)sizeof(struct sockaddr_in);

  struct epoll_event ev = {
      .events = EPOLLIN,
      .data.fd = server->sd,
  };
  struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 100000000,
  };

  sigset_t ss = {0};
  sigemptyset(&ss);
  sigaddset(&ss, SIGINT);

  if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->sd, &ev) < 0) {
    fprintf(stderr,
            "ERROR: failed to register event for tcp socket, due to error %s",
            strerror(errno));
    return -1;
  }

  while (1) {
    if (g_stop == 1) {
      break;
    }

    int events_ready =
        epoll_pwait2(server->epoll_fd, events, NUM_EPOLL_EVENTS, &timeout, &ss);
    if (events_ready == 0) {
      continue;
    } else if (events_ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "ERROR: failed to read events, due to error %s\n",
              strerror(errno));
      return -1;
    }

    for (int i = 0; i < events_ready; ++i) {
      if (events[i].data.fd == server->sd) {
        for (;;) {
          int cd =
              accept(server->sd, (struct sockaddr *)&in_addr, &in_addr_len);
          if (cd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            if (errno == EINTR) {
              continue;
            }
            fprintf(stderr,
                    "ERROR: failed to accept connection, due to error %s\n",
                    strerror(errno));
            break;
          }

          int n_req = 0;
          while (n_req < server->max_concurrent_http_requests &&
                 server->requests[n_req].cd != 0) {
            n_req++;
          }
          if (n_req == server->max_concurrent_http_requests) {
            fprintf(stderr, "No free HttpRequest slots; closing client\n");
            close(cd);
            continue;
          }

          init_http_request(&server->requests[n_req]);
          server->requests[n_req].cd = cd;

          ev.events = EPOLLIN | EPOLLRDHUP;
          ev.data.ptr = &server->requests[n_req];
          if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, cd, &ev) < 0) {
            fprintf(stderr, "Failed to register event, due to error %s\n",
                    strerror(errno));
            close(cd);
            server->requests[n_req].cd = 0;
            continue;
          }
        }
      } else {
        if ((events[i].events & EPOLLIN) == EPOLLIN) {
          struct HttpRequest *req = (struct HttpRequest *)events[i].data.ptr;
          int cd = req->cd;

          int read_bytes = read(cd, req->buffer, HTTP_MAX_BUFFER_SIZE);
          if (read_bytes < 0) {
            fprintf(stderr, "failed to read from socket, due to error %s\n",
                    strerror(errno));
          } else if (read_bytes == 0) {
            // peer closed, EPOLLRDHUP will handle cleanup
          } else {
            req->buffer_length = (size_t)read_bytes;
            enum HttpStatus status =
                make_http_request(req->buffer, read_bytes, req);
            if (status == HTTP_OK) {
              server->handler(req);
            } else {
              struct HttpResponse response = {0};
              response.status = status;
              write_http_response(req, &response);
            }
          }
        }

        if ((events[i].events & EPOLLRDHUP) == EPOLLRDHUP) {
          struct HttpRequest *req = (struct HttpRequest *)events[i].data.ptr;
          int cd = req->cd;
          req->cd = 0;
          if (close(cd) < 0) {
            fprintf(stderr, "failed to close accept socket, due to error %s\n",
                    strerror(errno));
          }
          epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, cd, NULL);
        }
      }
    }
  }
  return 0;
}

void end_http_server(struct HttpServer *server) {
  if (close(server->sd) < 0) {
    fprintf(stderr, "ERROR: unable to close socket, due to error %s\n",
            strerror(errno));
  }
  if (close(server->epoll_fd) < 0) {
    fprintf(stderr, "failed to close epoll, due to error %s\n",
            strerror(errno));
  }
  for (int i = 0; i < server->max_concurrent_http_requests; ++i) {
    http_free_request(&server->requests[i]);
  }
  free(server->requests);
  return;
}

inline void write_http_response(const struct HttpRequest *req,
                                const struct HttpResponse *res) {
  char header_buffer[HTTP_MAX_BUFFER_SIZE];
  const char *status_str = "500 Internal Server Error";
  switch (res->status) {
  case HTTP_OK:
    status_str = "200 OK";
    break;
  case HTTP_BAD_REQUEST:
    status_str = "400 Bad Request";
    break;
  case HTTP_NOT_FOUND:
    status_str = "404 Not Found";
    break;
  case HTTP_VERSION_NOT_SUPPORTED:
    status_str = "505 HTTP Version Not Supported";
    break;
  }

  int header_len =
      snprintf(header_buffer, HTTP_MAX_BUFFER_SIZE,
               "%s %s\r\n"
               "Content-Length: %zu\r\n"
               "Content-Type: %s\r\n"
               "\r\n", // End of headers
               HTTP_SUPPORTED_VERSION, status_str, res->content_length,
               HTTP_CONTENT_TYPE_STR[res->content_type]);

  if (header_len < 0 || header_len >= HTTP_MAX_BUFFER_SIZE) {
    fprintf(stderr, "Error: Header too long!\n");
  }

  vec[0].iov_base = header_buffer;
  vec[0].iov_len = (size_t)header_len;

  if (res->content_length > 0) {
    vec[1].iov_base = (void *)res->content;
    vec[1].iov_len = res->content_length;
    writev(req->cd, vec, 2);
  } else {
    writev(req->cd, vec, 1);
  }
}
