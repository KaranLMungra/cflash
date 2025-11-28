#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_PROTOCOL 0x06
#define TCP_ADDR 0x0
#define TCP_PORT 8000
#define TCP_BACKLOG 1024
#define BUFF_SIZE 512
#define STACK_SIZE 8192
#define NUM_THREADS 100
#define WORKER_THREADS 100

struct thread_info {   /* Used as argument to thread_start() */
  pthread_t thread_id; /* ID returned by pthread_create() */
  int sd;              /* Socket descriptor */
};

const char *CONTENT_200 =
    "HTTP/1.1 200 OK\r\nServer: Server/1.0\r\nContent-Language: "
    "en\r\nContent-Length: 13\r\nConnection: close\r\nContent-Type: "
    "text/plain\r\n\r\nHello, World!";
const char *CONTENT_400 = "HTTP/1.1 400 Bad Request\r\nServer: "
                          "Server/1.0\r\nConnection: close\r\n\r\n";
const char *CONTENT_404 =
    "HTTP/1.1 404 Not Found\r\nServer: Server/1.0\r\nConnection: close\r\n\r\n";
const char *CONTENT_405 = "HTTP/1.1 405 Method Not Allowed\r\nServer: "
                          "Server/1.0\r\nConnection: close\r\n\r\n";
const char *CONTENT_500 = "HTTP/1.1 500 Internal Server Error\r\nServer: "
                          "Server/1.0\r\nConnection: close\r\n\r\n";
const char *CONTENT_503 = "HTTP/1.1 503 Service Unavailable\r\nServer: "
                          "Server/1.0\r\nConnection: close\r\n\r\n";
const char *CONTENT_505 = "HTTP/1.1 505 HTTP Version not supported\r\nServer: "
                          "Server/1.0\r\nConnection: close\r\n\r\n";

enum ECHO_SERVER_ERRORS {
  FAILED_SOCKET_CLOSE = -8,
  FAILED_SOCKET_WRITE,
  FAILED_SOCKET_READ,
  FAILED_SOCKET_ACCEPT,
  FAILED_SOCKET_LISTEN,
  FAILED_SOCKET_BIND,
  FAILED_SOCKET_SET_OPTION,
  FAILED_SOCKET_CREATE,
  UNKNOWN
};
static struct thread_info tinfo[NUM_THREADS];

int handle_http(char *buffer, int read_bytes) {
  char *r = buffer;
  int i = 0;
  // Handling: Method
  while (i <= read_bytes && buffer[i] != ' ') {
    i++;
  }
  if (i == read_bytes) {
    fprintf(stderr, "Failed to parse http request");
    return 400;
  }
  buffer[i] = 0;
  // printf("Method: %s\n", r);
  if (strcmp("GET", r) != 0) {
    return 405;
  }
  buffer[i] = ' ';
  r = &buffer[++i];
  // Handling: Path
  while (i <= read_bytes && buffer[i] != ' ') {
    i++;
  }
  if (i == read_bytes) {
    // fprintf(stderr, "Failed to parse http request");
    return 400;
  }
  buffer[i] = 0;
  // printf("Path: %s\n", r);
  if (strcmp("/", r) != 0) {
    return 404;
  }
  buffer[i] = ' ';
  r = &buffer[++i];
  // Handling: Version
  while (i <= read_bytes && buffer[i] != '\r') {
    i++;
  }
  if (i == read_bytes) {
    fprintf(stderr, "Failed to parse http request");
    return 400;
  }
  buffer[i] = 0;
  // printf("Version: %s\n", r);
  if (strcmp("HTTP/1.1", r) != 0) {
    return 505;
  }
  buffer[i] = '\r';
  i += 2;
  r = &buffer[i];
  // Handling headers
  // printf("Headers:\n");
  while (i <= read_bytes && buffer[i] != '\r') {
    while (i <= read_bytes && buffer[i] != '\r') {
      i++;
    }
    buffer[i] = 0;
    // printf("%s\n", r);
    buffer[i] = '\r';
    i += 2;
    r = &buffer[i];
  }
  i += 2;
  r = &buffer[i];
  // printf("Content:\n");
  // printf("%s", r);
  return 200;
}

void handle_response(int sd, int ret, enum ECHO_SERVER_ERRORS server_errno) {
  if (ret >= 0) {
    return;
  }
  fprintf(stderr, "Server errno: %d", server_errno);
  switch (server_errno) {
  case FAILED_SOCKET_CREATE:
    fprintf(stderr, "Failed to create tcp socket, due to error %s\n",
            strerror(errno));
    exit(server_errno);

  case FAILED_SOCKET_SET_OPTION:
    fprintf(stderr, "Failed to set option for tcp socket, due to error %s\n",
            strerror(errno));
    break;
  case FAILED_SOCKET_BIND:
    fprintf(stderr, "Failed to bind tcp socket, due to error %s\n",
            strerror(errno));
    break;
  case FAILED_SOCKET_LISTEN:
    fprintf(stderr, "Failed to listen on tcp socket, due to error %s\n",
            strerror(errno));
    break;
  case FAILED_SOCKET_ACCEPT:
    fprintf(stderr, "Failed to accept on tcp socket, due to error %s\n",
            strerror(errno));
    exit(errno);
  case FAILED_SOCKET_READ:
    fprintf(stderr,
            "Failed to read on tcp socket, due to error %s and errcode %d\n",
            strerror(errno), errno);
    break;

  case FAILED_SOCKET_WRITE:
    fprintf(stderr,
            "Failed to write on tcp socket, due to error %s and errcode %d\n",
            strerror(errno), errno);
    break;
  case FAILED_SOCKET_CLOSE:
    fprintf(stderr, "Failed to close tcp socket, due to error %s\n",
            strerror(errno));
    exit(server_errno);
  case UNKNOWN:
    fprintf(stderr, "Error Happened at unknown place %s\n", strerror(errno));
    break;
  }
  if (close(sd) == -1) {
    fprintf(stderr, "Failed to close tcp socket, due to error %s\n",
            strerror(errno));
    exit(server_errno);
  }
}

int handle_http_status(int sd, int status) {
  switch (status) {
  case 200:
    return write(sd, CONTENT_200, strlen(CONTENT_200));
  case 400:
    return write(sd, CONTENT_400, strlen(CONTENT_400));
  case 404:
    return write(sd, CONTENT_404, strlen(CONTENT_404));
  case 405:
    return write(sd, CONTENT_405, strlen(CONTENT_405));
  case 503:
    return write(sd, CONTENT_503, strlen(CONTENT_503));
  case 505:
    return write(sd, CONTENT_505, strlen(CONTENT_505));
  default:
    fprintf(stderr, "Status code = %d, not supported yet.", status);
    return write(sd, CONTENT_500, strlen(CONTENT_500));
  }
}

void *handle_conn(void *arg) {

  char buffer[BUFF_SIZE];
  struct thread_info *info = (struct thread_info *)arg;
  socklen_t addrlen = sizeof(struct sockaddr_in);

  for (;;) {
    struct sockaddr_in client_addr = {0};
    int cd = accept(info->sd, (struct sockaddr *)&client_addr, &addrlen);
    if (cd < 0) {
      fprintf(stderr, "Failed to accept tcp connection, due to error %s",
              strerror(errno));
    }
    printf("Accepted connection from %s:%d\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    // TODO: This must be a loop, until we get EOF
    memset(buffer, 0, BUFF_SIZE);
    int read_bytes = read(cd, buffer, BUFF_SIZE - 1);
    if (read_bytes == 0) {
    } else if (read_bytes == -1) {
      fprintf(stderr, "Failed to read from tcp socket, due to error %s\n",
              strerror(errno));
    } else {
      buffer[read_bytes] = 0;
      int status = handle_http(buffer, read_bytes);
      printf("Status code = %d\n", status);
      if (handle_http_status(cd, status) == -1) {
        fprintf(stderr, "Failed to write to tcp socket, due to error %s\n",
                strerror(errno));
      }
    }
    if (close(cd) == -1) {
      fprintf(stderr, "Failed to close tcp socket, due to error %s\n",
              strerror(errno));
    }
  }
  return NULL;
}

int main(void) {
  pthread_attr_t attr = {0};
  void *res;
  int optval = 1;
  printf("Trying to create TCP socket...\n");
  int sd = socket(AF_INET, SOCK_STREAM, TCP_PROTOCOL);
  handle_response(sd, sd, FAILED_SOCKET_CREATE);
  handle_response(
      sd, setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (int *)&optval, sizeof(int)),
      FAILED_SOCKET_SET_OPTION);
  printf("Socket option SO_REUSEADDR = %d\n", optval);

  handle_response(sd, pthread_attr_init(&attr), UNKNOWN);
  handle_response(sd, pthread_attr_setstacksize(&attr, STACK_SIZE), UNKNOWN);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = TCP_ADDR},
      .sin_port = htons(TCP_PORT),
  };
  printf("TCP socket created successfully, socket = %d\n", sd);
  handle_response(
      sd, bind(sd, (struct sockaddr *)(&addr), sizeof(struct sockaddr_in)),
      FAILED_SOCKET_BIND);
  printf("Successfully binded TCP socket to address %s and port %d\n",
         inet_ntoa((struct in_addr){.s_addr = TCP_ADDR}), TCP_PORT);
  handle_response(sd, listen(sd, TCP_BACKLOG), FAILED_SOCKET_LISTEN);
  printf("Listening for incoming connections...\n");
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    tinfo[i].sd = sd;
    handle_response(sd,
                    pthread_create(&tinfo[i].thread_id, &attr, &handle_conn,
                                   (void *)&tinfo[i]),
                    UNKNOWN);
  }

  // TODO: Add signal handler Ctrl+C
  handle_response(sd, pthread_attr_destroy(&attr), UNKNOWN);
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    handle_response(sd, pthread_join(tinfo[i].thread_id, &res), UNKNOWN);
  }
  handle_response(sd, close(sd), FAILED_SOCKET_CLOSE);
  printf("TCP socket closed successfully, socket = %d\n", sd);
  return 0;
}
