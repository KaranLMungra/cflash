#include "http.h"
#include <string.h>

#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"
#define MAX_CONCURRENT_HTTP_REQUESTS 1024

void http_handler(const struct HttpRequest *request,
                  struct HttpResponse *response) {
    if(request->method == HTTP_POST && (strncmp(request->path, "/echo", 5) == 0)) {
        response->status = HTTP_OK;
    } else {
        response->status = HTTP_BAD_REQUEST;
    }
}

int main(void) {
  struct HttpServer server = {0};
  if (begin_http_server(&server, SERVER_HOST, SERVER_PORT,
                        MAX_CONCURRENT_HTTP_REQUESTS, &http_handler) < 0) {
    return -1;
  }
  run_http_server(&server);
  end_http_server(&server);
  return 0;
}
