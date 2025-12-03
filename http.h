#ifndef HTTP_INCLUDE
#define HTTP_INCLUDE

#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>

#define HTTP_MAX_BUFFER_SIZE 4096
#define HTTP_SUPPORTED_VERSION "HTTP/1.1"
#define HTTP_SUPPORTED_VERSION_LEN 8
#define HTTP_INVALID -1
#define MAX_HEADERS 32

#define HTTP_HEADER_NAME_MAX 64
#define HTTP_HEADER_VALUE_MAX 512
#define HTTP_MAX_PATH_LENGTH 1024

enum HttpMethod { HTTP_GET = 0, HTTP_POST, HTTP_METHOD_NUM };

static const char *HTTP_METHODS_STR[HTTP_METHOD_NUM] = {"GET", "POST"};

struct HttpHeader {
  const char *name;
  size_t name_length;
  const char *value;
  size_t value_length;
};

enum HttpStatus {
  HTTP_OK = 200,
  HTTP_BAD_REQUEST = 400,
  HTTP_NOT_FOUND = 404,
  HTTP_VERSION_NOT_SUPPORTED = 505,
};

static const char *HTTP_REASON_OK = "OK";
static const int HTTP_REASON_OK_LEN = 2;
static const char *HTTP_REASON_BAD_REQUEST = "BAD REQUEST";
static const int HTTP_REASON_BAD_REQUEST_LEN = 11;
static const char *HTTP_REASON_NOT_FOUND = "NOT FOUND";
static const int HTTP_REASON_NOT_FOUND_LEN = 9;
static const char *HTTP_REASON_VERSION_NOT_SUPPORTED = "VERSION_NOT_SUPPORTED";
static const int HTTP_REASON_VERSION_NOT_SUPPORTED_LEN = 21;

struct HttpRequest {
  int cd;
  enum HttpMethod method;

  const char *path;
  size_t path_length;

  struct HttpHeader *headers;
  size_t num_headers;

  const char *body;
  size_t body_length;

  char buffer[HTTP_MAX_BUFFER_SIZE];
  size_t buffer_length;
};

struct HttpResponse {
  enum HttpStatus status;
};

typedef void (*HttpHandler)(const struct HttpRequest *request,
                            struct HttpResponse*);
struct HttpServer {
  int sd;
  int epoll_fd;
  size_t max_concurrent_http_requests;
  struct sockaddr_in addr;
  struct HttpRequest *requests;
  HttpHandler handler;
};

int begin_http_server(struct HttpServer *server, const char *server_host,
                      int server_port, size_t max_concurrent_http_requests, HttpHandler handler);
int run_http_server(struct HttpServer *server);
void end_http_server(struct HttpServer *server);

void init_http_request(struct HttpRequest *req);
enum HttpStatus make_http_request(const char *buffer, int buf_len,
                                  struct HttpRequest *req);
void print_http_request(const struct HttpRequest *req);
void http_free_request(struct HttpRequest *req);

#endif
