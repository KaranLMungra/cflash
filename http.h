#include <stdlib.h>

#ifndef HTTP_INCLUDE
#define HTTP_INCLUDE
#define HTTP_MAX_BUFFER_SIZE 4096
#define HTTP_SUPPORTED_VERSION "HTTP/1.1"
#define HTTP_INVALID -1
#define MAX_HEADERS 32

enum HttpMethod { HTTP_GET = 0, HTTP_POST, HTTP_METHOD_NUM };

static const char *HTTP_METHODS_STR[HTTP_METHOD_NUM] = {"GET", "POST"};

struct HttpHeader {
  char name[HTTP_MAX_BUFFER_SIZE];
  char value[HTTP_MAX_BUFFER_SIZE];
};

enum HttpStatus {
  HTTP_OK = 200,
  HTTP_BAD_REQUEST = 400,
  HTTP_NOT_FOUND = 404,
  HTTP_VERSION_NOT_SUPPORTED = 505,
};

struct HttpRequest {
    int cd;
  enum HttpMethod method;
  char path[HTTP_MAX_BUFFER_SIZE];
  struct HttpHeader *headers;
  size_t num_headers;
  char body[HTTP_MAX_BUFFER_SIZE];
  size_t body_length;
};

void init_http_request(struct HttpRequest *req, int cd);
enum HttpStatus make_http_request(const char *buffer, int buf_len,
                                  struct HttpRequest *req);

void print_http_request(const struct HttpRequest *req);
void http_free_request(struct HttpRequest* req);
void reset_http_request(struct HttpRequest *req);
#endif
