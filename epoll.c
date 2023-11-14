#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

#define MAXEVENTS 64
#define THREAD_NUM 4
#define PORT 9000

typedef struct epoll_info
{
    int epoll_fd;
    struct epoll_event event;
    struct epoll_event* events;
} epoll_info;

epoll_info epolls[THREAD_NUM];
int taskQueue[100000];
int taskCount = 0;
int sock;

pthread_mutex_t mutexQueue;
pthread_cond_t condQueue;

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl()");
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl()");
  }
}


void submitTask(int task)
{
    pthread_mutex_lock(&mutexQueue);
    taskQueue[taskCount++] = task;
    pthread_mutex_unlock(&mutexQueue);
    pthread_cond_signal(&condQueue);
}

void* handleConnection(void* args)
{
    epoll_info* epoll_in = (epoll_info*) args;
    bool active = true;
    for (;;) {
        if (!active) usleep(100);
        int nevents = epoll_wait(epoll_in->epoll_fd, epoll_in->events, MAXEVENTS, -1);
        if (nevents == -1) {
            perror("epoll_wait()");
            exit(EXIT_FAILURE);
        } else if (nevents == 0) {
            active = false;
            continue;
        }
        active = true;
        for (int i = 0; i < nevents; i++) {
            if ((epoll_in->events[i].events & EPOLLERR) || (epoll_in->events[i].events & EPOLLHUP) ||
                (!(epoll_in->events[i].events & EPOLLIN))) {
                // error case
                fprintf(stderr, "epoll error\n");
                close(epoll_in->events[i].data.fd);
                continue;
            } else if (epoll_in->events[i].data.fd == sock) {
                // server socket; call accept as many times as we can
                pthread_mutex_lock(&mutexQueue);
                while (taskCount == 0) {
                    pthread_cond_wait(&condQueue, &mutexQueue);
                }
                int client = taskQueue[taskCount-1];
                taskCount--;
                pthread_mutex_unlock(&mutexQueue);

                if (client == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // we processed all of the connections
                    break;
                    } else {
                        perror("accept()");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    printf("accepted new connection on fd %d\n", client);
                    set_nonblocking(client);
                    epoll_in->event.data.fd = client;
                    epoll_in->event.events = EPOLLIN | EPOLLET;
                    if (epoll_ctl(epoll_in->epoll_fd, EPOLL_CTL_ADD, client, &epoll_in->event) == -1) {
                        perror("epoll_ctl()");
                        exit(EXIT_FAILURE);
                    }
                }
                
            } else {
                // client socket; read as much data as we can
                char buf[1024];
                for (;;) {
                    ssize_t nbytes = read(epoll_in->events[i].data.fd, buf, sizeof(buf));
                    if (nbytes == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("finished reading data from client\n");
                        break;
                        } else {
                            perror("read()");
                            exit(EXIT_FAILURE);
                        }
                    } else if (nbytes == 0) {
                        printf("finished with %d\n", epoll_in->events[i].data.fd);
                        while (true) {
                            
                        }
                        close(epoll_in->events[i].data.fd);
                        break;
                    } else {
                        fwrite(buf, sizeof(char), nbytes, stdout);
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv) {
  // create the server socket
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("socket()");
    return 1;
  }
  int enable = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) ==
      -1) {
    perror("setsockopt()");
    return 1;
  }

  // bind
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(PORT);
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind()");
    return 1;
  }

  // make it nonblocking, and then listen
  set_nonblocking(sock);
  if (listen(sock, SOMAXCONN) < 0) {
    perror("listen()");
    return 1;
  }

  for (int i=0; i<THREAD_NUM; i++) {
      // create the epoll socket
    epolls[i].epoll_fd = epoll_create1(0);
    if (epolls[i].epoll_fd == -1) {
        perror("epoll_create1()");
        return 1;
    }

    // mark the server socket for reading, and become edge-triggered
    memset(&epolls[i].event, 0, sizeof(epolls[i].event));
    epolls[i].event.data.fd = sock;
    epolls[i].event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epolls[i].epoll_fd, EPOLL_CTL_ADD, sock, &epolls[i].event) == -1) {
        perror("epoll_ctl()");
        return 1;
    }

    epolls[i].events = calloc(MAXEVENTS, sizeof(epolls[i].event));
  }

    pthread_mutex_init(&mutexQueue, NULL);
    pthread_t th[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(&th[i], NULL, &handleConnection, (void *) &epolls[i]) != 0) {
            perror("Failed to create a thread");
        }
    }
    bool active = true;
    while(1)
    {
        // printf("\n************* Waiting for new connection *************\n\n");
        if (!active) usleep(100);
        int new_socket;
        if ((new_socket = accept(sock, (struct sockaddr *)&addr, (socklen_t*)&addrlen)) == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // we processed all of the connections
              active = false;
                continue;
            } else {
              perror("accept()");
              return 1;
            }
        }
        active = true;
        submitTask(new_socket);

    }
    
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join the thread");
        }
    }
    pthread_mutex_destroy(&mutexQueue);
    close(sock);
    return 0;
}