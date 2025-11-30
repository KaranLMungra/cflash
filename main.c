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
#include <time.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"
#define MAX_BUFFER_SIZE 4096
#define NUM_EPOLL_EVENTS 10
#define NUM_IOVEC 2
static char buffer[MAX_BUFFER_SIZE];
static struct epoll_event events[NUM_EPOLL_EVENTS] = {0};

struct ServerContext {
  int epoll;
  int sd;
};

void signal_handler(int signal, siginfo_t *info, void *context) {
  printf("Gracefully  shutdown signal recieved");
  struct ServerContext *server_context = (struct ServerContext *)context;
  int epoll = server_context->epoll;
  int sd = server_context->sd;
  if (epoll >= 0) {
    if (close(epoll) < 0) {
      fprintf(stderr, "Failed to close epoll, due to error %s\n",
              strerror(errno));
    }
  }
  if (sd >= 0) {
    if (close(sd) < 0) {
      fprintf(stderr, "Failed to close socket, due to error %s\n",
              strerror(errno));
    }
  }
  exit(EXIT_SUCCESS);
}

int main(void) {
  int sd;
  int err;
  int optval = 1;
  int epoll = epoll_create1(0);
  struct sigaction act = {0};
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = &signal_handler;
  struct iovec vec[NUM_IOVEC];

  if (epoll < 0) {
    fprintf(stderr, "Unable to create epoll, due to error %s\n",
            strerror(errno));
    goto exit;
  }

  sd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (sd < 0) {
    fprintf(stderr, "Unable to create socket, due to error %s\n",
            strerror(errno));
    err = sd;
    goto exit;
  }
  printf("Successfully created socket: %d\n", sd);
  err = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  if (err < 0) {
    fprintf(stderr, "Unable to set option socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(SERVER_PORT),
      .sin_addr = inet_addr(SERVER_HOST),
  };
  err = bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
  if (err < 0) {
    fprintf(stderr, "Unable to bind socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }
  printf("Successfully, bound the socket to %s:%d\n", SERVER_HOST, SERVER_PORT);
  err = listen(sd, SOMAXCONN);
  if (err < 0) {
    fprintf(stderr, "Unable to lisen socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }
  printf("Successfully, listening for incoming connections...\n");
  struct ServerContext context = {.epoll = epoll, .sd = sd};
  if (sigaction(SIGINT, &act, (void *)&context) < 0) {
    fprintf(stderr, "Unable to register signal handler, due to error %s\n",
            strerror(errno));
    goto exit;
  }
  struct sockaddr_in in_addr;
  socklen_t in_addr_len = (socklen_t)sizeof(struct sockaddr_in);
  struct epoll_event ev = {
      .events = EPOLLIN,
      .data.fd = sd,
  };
  struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 100000000,
  };

  sigset_t ss = {0};
  sigemptyset(&ss);
  sigaddset(&ss, SIGINT);
  epoll_ctl(epoll, EPOLL_CTL_ADD, sd, &ev);
  while (1) {
    int events_ready =
        epoll_pwait2(epoll, events, NUM_EPOLL_EVENTS, &timeout, &ss);
    if (events_ready == 0) {
      continue;
    } else if (events_ready < 0) {
      fprintf(stderr, "Failed to read events, due to error %s\n",
              strerror(errno));
      continue;
    }
    for (int i = 0; i < events_ready; ++i) {
      if (events[i].data.fd == sd) {
        int cd = accept(sd, (struct sockaddr *)&in_addr, &in_addr_len);
        if (cd < 0) {
          fprintf(stderr, "Failed to accept connection, due to error %s\n",
                  strerror(errno));
          continue;
        }
        struct HttpRequest *req = calloc(1, sizeof(struct HttpRequest));
        init_http_request(req, cd);
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.ptr = req;
        epoll_ctl(epoll, EPOLL_CTL_ADD, cd, &ev);
      } else {
        if ((events[i].events & EPOLLIN) == EPOLLIN) {
          struct HttpRequest *req = (struct HttpRequest *)events[i].data.ptr;
          int cd = req->cd;
          memset(buffer, 0, MAX_BUFFER_SIZE);
          int read_bytes = read(cd, buffer, MAX_BUFFER_SIZE - 1);
          if (read_bytes < 0) {
            fprintf(stderr, "Failed to read from socket, due to error %s\n",
                    strerror(errno));
          } else {
            enum HttpStatus status =
                make_http_request(buffer, read_bytes + 1, req);
            if (status == HTTP_OK) {
              int n = snprintf(buffer, MAX_BUFFER_SIZE - 1,
                      "HTTP/1.1 200 0K\r\nContent-Length: "
                      "%ld\r\nContent-Type: text/plain\r\n\r\n",
                      req->body_length);
              vec[0].iov_base = buffer;
              vec[0].iov_len = n;
              vec[1].iov_base = req->body;
              vec[1].iov_len = req->body_length;

              writev(cd, vec, NUM_IOVEC);
            }
            reset_http_request(req);
          }
        }
        if ((events[i].events & EPOLLRDHUP) == EPOLLRDHUP) {
          struct HttpRequest *req = (struct HttpRequest *)events[i].data.ptr;
          int cd = req->cd;
          http_free_request(req);
          free(req);
          if (close(cd) < 0) {
            fprintf(stderr, "Failed to close accept socket, due to error %s\n",
                    strerror(errno));
          } else {
          }
          epoll_ctl(epoll, EPOLL_CTL_DEL, cd, NULL);
        }
      }
    }
  }
exit:
  if (epoll >= 0) {
    if (close(epoll) < 0) {
      fprintf(stderr, "Failed to close epoll, due to error %s\n",
              strerror(errno));
    }
  }
  if (sd >= 0) {
    if (close(sd) < 0) {
      fprintf(stderr, "Failed to close socket, due to error %s\n",
              strerror(errno));
    }
  }
  printf("Successfully closed socket: %d\n", sd);
  return err;
}
