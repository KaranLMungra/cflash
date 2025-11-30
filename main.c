#include "http.h"
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
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
#define MAX_CONCURRENT_HTTP_REQUESTS 1024
#define NUM_EPOLL_EVENTS 10
#define NUM_IOVEC 2

static struct epoll_event events[NUM_EPOLL_EVENTS] = {0};
static volatile sig_atomic_t g_stop = 0;

void signal_handler(int signal, siginfo_t *info, void *context) {
  (void)signal;
  (void)info;
  (void)context;
  printf("gracefully  shutdown signal recieved\n");
  g_stop = 1;
}

static inline char *u64_to_str(char *end, size_t v) {
  // write digits backwards
  char *p = end;
  do {
    *--p = (char)('0' + (v % 10));
    v /= 10;
  } while (v);
  return p;
}

int main(void) {
  int sd = -1;
  int err;
  int optval = 1;
  int epoll_fd = epoll_create1(0);
  struct sigaction act = {0};
  act.sa_flags = SA_SIGINFO;
  act.sa_sigaction = &signal_handler;
  struct iovec vec[NUM_IOVEC];
  struct HttpRequest *requests =
      calloc(MAX_CONCURRENT_HTTP_REQUESTS, sizeof(struct HttpRequest));
  for (int i = 0; i < MAX_CONCURRENT_HTTP_REQUESTS; ++i) {
    init_http_request(&requests[i]);
  }

  if (epoll_fd < 0) {
    fprintf(stderr, "unable to create epoll, due to error %s\n",
            strerror(errno));
    goto exit;
  }

  sd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (sd < 0) {
    fprintf(stderr, "unable to create socket, due to error %s\n",
            strerror(errno));
    err = sd;
    goto exit;
  }
  err = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  if (err < 0) {
    fprintf(stderr, "unable to set option socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(SERVER_PORT),
      .sin_addr = {inet_addr(SERVER_HOST)},
  };
  err = bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
  if (err < 0) {
    fprintf(stderr, "unable to bind socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }
  printf("starting server at %s:%d\n", SERVER_HOST, SERVER_PORT);
  err = listen(sd, SOMAXCONN);
  if (err < 0) {
    fprintf(stderr, "unable to lisen socket, due to error %s\n",
            strerror(errno));
    goto exit;
  }
  printf("listening for incoming connections...\n");
  if (sigaction(SIGINT, &act, NULL) < 0) {
    fprintf(stderr, "unable to register signal handler, due to error %s\n",
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

  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sd, &ev);

  while (1) {
    if (g_stop == 1) {
      break;
    }

    int events_ready =
        epoll_pwait2(epoll_fd, events, NUM_EPOLL_EVENTS, &timeout, &ss);
    if (events_ready == 0) {
      continue;
    } else if (events_ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "failed to read events, due to error %s\n",
              strerror(errno));
      goto exit;
    }

    for (int i = 0; i < events_ready; ++i) {
      if (events[i].data.fd == sd) {
        for (;;) {
          int cd = accept(sd, (struct sockaddr *)&in_addr, &in_addr_len);
          if (cd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            if (errno == EINTR) {
              continue;
            }
            fprintf(stderr, "failed to accept connection, due to error %s\n",
                    strerror(errno));
            break;
          }

          int n_req = 0;
          while (n_req < MAX_CONCURRENT_HTTP_REQUESTS &&
                 requests[n_req].cd != 0) {
            n_req++;
          }
          if (n_req == MAX_CONCURRENT_HTTP_REQUESTS) {
            fprintf(stderr, "No free HttpRequest slots; closing client\n");
            close(cd);
            continue;
          }

          requests[n_req].cd = cd;
          requests[n_req].buffer_length = 0;
          requests[n_req].num_headers = 0;
          requests[n_req].path = NULL;
          requests[n_req].path_length = 0;
          requests[n_req].body = NULL;
          requests[n_req].body_length = 0;

          ev.events = EPOLLIN | EPOLLRDHUP;
          ev.data.ptr = &requests[n_req];
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cd, &ev) < 0) {
            fprintf(stderr, "Failed to register event, due to error %s\n",
                    strerror(errno));
            close(cd);
            requests[n_req].cd = 0;
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

              char *p = req->buffer;

              const char prefix[] = "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: ";
              memcpy(p, prefix, sizeof(prefix) - 1);
              p += sizeof(prefix) - 1;

              char *len_end = p + 20; // enough for 64-bit
              char *len_start = u64_to_str(len_end, req->body_length);
              size_t len_len = (size_t)(len_end - len_start);
              memcpy(p, len_start, len_len);
              p += len_len;

              const char suffix[] = "\r\nContent-Type: text/plain\r\n\r\n";
              memcpy(p, suffix, sizeof(suffix) - 1);
              p += sizeof(suffix) - 1;

              vec[0].iov_base = req->buffer;
              vec[0].iov_len = (size_t)(p - req->buffer);
              vec[1].iov_base = (void *)req->body;
              vec[1].iov_len = req->body_length;
              writev(cd, vec, NUM_IOVEC);
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
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cd, NULL);
        }
      }
    }
  }

exit:
  if (epoll_fd >= 0) {
    if (close(epoll_fd) < 0) {
      fprintf(stderr, "failed to close epoll, due to error %s\n",
              strerror(errno));
    }
  }
  if (sd >= 0) {
    if (close(sd) < 0) {
      fprintf(stderr, "failed to close socket, due to error %s\n",
              strerror(errno));
    }
  }
  for (int i = 0; i < MAX_CONCURRENT_HTTP_REQUESTS; ++i) {
    http_free_request(&requests[i]);
  }
  free(requests);
  return err;
}
