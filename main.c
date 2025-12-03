#include "http.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"
#define MAX_CONCURRENT_HTTP_REQUESTS 1024
#define REAL_JSON_BUFFER 20480
static char real_json[REAL_JSON_BUFFER] = {0};
static size_t real_json_length = 0;

void http_handler(const struct HttpRequest *request) {
  struct HttpResponse response = {0};
  if (request->method == HTTP_POST &&
      (strncmp(request->path, "/echo", 5) == 0)) {
    response.status = HTTP_OK;
    response.content = request->body;
    response.content_length = request->body_length;
    response.content_type = HTTP_TEXT_PLAIN;
  } else if (request->method == HTTP_GET &&
             (strncmp(request->path, "/service-info", 13) == 0)) {
    response.status = HTTP_OK;
    response.content = real_json;
    response.content_length = real_json_length;
    response.content_type = HTTP_APPLICATION_JSON;
  } else {
    response.status = HTTP_BAD_REQUEST;
  }
  write_http_response(request, &response);
  printf("[INFO] :: %s STATUS %d\n", HTTP_METHODS_STR[request->method],
         response.status);
}

int read_real_json() {
  FILE *fp = fopen("real.json", "r");
  if (fp == NULL) {
    fprintf(stderr, "Failed to open file real.json, due to error %s\n",
            strerror(errno));
    return -1;
  }
  size_t bytes_read = fread((void *)real_json, REAL_JSON_BUFFER, 1, fp);
  if (ferror(fp) > 0) {
    fprintf(stderr, "Failed to read from file real.json, due to error %s\n",
            strerror(errno));
    fclose(fp);
    return -1;
  }
  if (feof(fp) > 0) {
    real_json_length = strlen(real_json);
  } else {
    real_json_length = bytes_read;
  }
  fclose(fp);
  return 0;
}

int main(void) {
  if (read_real_json() < 0) {
    return -1;
  }
  struct HttpServer server = {0};
  if (begin_http_server(&server, SERVER_HOST, SERVER_PORT,
                        MAX_CONCURRENT_HTTP_REQUESTS, &http_handler) < 0) {
    return -1;
  }
  run_http_server(&server);
  end_http_server(&server);
  return 0;
}
