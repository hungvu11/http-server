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

#define MAXEVENTS 10000
#define THREAD_NUM 5
#define PORT 9000

typedef struct epoll_info
{
    int epoll_fd;
    struct epoll_event* events;
} epoll_info;

char response[78] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nContent-Type: text/plain\r\n\r\nHello, world";

epoll_info epolls[THREAD_NUM];

int sock;

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

void control_epoll_event(int epoll_fd, int op, int fd, uint32_t events)
{
    if (op == EPOLL_CTL_DEL) {
        if (epoll_ctl(epoll_fd, op, fd, NULL) < 0) {
            perror("del event");
            exit(EXIT_FAILURE);
        }
    } else {
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, op, fd, &ev) < 0) {
            perror("add event");
            exit(EXIT_FAILURE);
        }
    }
}

void* handleConnection(void* args)
{
    epoll_info* epoll_in = (epoll_info*) args;
    int fd = epoll_in->epoll_fd;
    bool active = true;
    // printf("current fd %d\n", fd);
    for (;;) {
        if (!active) usleep(100);
        int nevents = epoll_wait(fd, epoll_in->events, MAXEVENTS, -1);
        if (nevents == -1) {
            perror("epoll_wait()");
            exit(EXIT_FAILURE);
        } else if (nevents == 0) {
            active = false;
            continue;
        }
        active = true;
        for (int i = 0; i < nevents; i++) {
            
            struct epoll_event ev = epoll_in->events[i];
            if ((ev.events & EPOLLERR) || (ev.events & EPOLLHUP)) {
                // error case
                // perror("epoll error");
                control_epoll_event(fd, EPOLL_CTL_DEL, ev.data.fd, 0);
                close(ev.data.fd);
            } else if (ev.events & EPOLLIN) {
                // read from client
                char buf[4096];
                ssize_t nbytes = recv(ev.data.fd, buf, sizeof(buf), 0);
                if (nbytes == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) { // retry
                        control_epoll_event(fd, EPOLL_CTL_MOD, ev.data.fd, EPOLLIN | EPOLLET);
                    } else { // other error
                        perror("read()");
                        control_epoll_event(fd, EPOLL_CTL_DEL, ev.data.fd, 0);
                        close(ev.data.fd);
                    }
                } else if (nbytes == 0) { // close connection
                    // printf("finished with %d\n", ev.data.fd);
                    control_epoll_event(fd, EPOLL_CTL_DEL, ev.data.fd, 0);
                    close(ev.data.fd);
                    // break;
                } else { // fully receive the message
                    control_epoll_event(fd, EPOLL_CTL_MOD, ev.data.fd, EPOLLOUT | EPOLLET);
                    // fwrite(buf, sizeof(char), nbytes, stdout);
                } 
            } else if (ev.events & EPOLLOUT) {
                // write a response to client
                int send_length = 0;
                int total_length = sizeof(response);
                
                ssize_t nbytes = send(ev.data.fd, response, sizeof(response), 0);
                // fwrite(response, sizeof(char), nbytes, stdout);
                if (nbytes == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) { // retry
                        control_epoll_event(fd, EPOLL_CTL_MOD, ev.data.fd, EPOLLOUT | EPOLLET);
                    } else {
                        perror("read()");
                        control_epoll_event(fd, EPOLL_CTL_DEL, ev.data.fd, 0);
                        close(ev.data.fd);
                    }
                } else {
                    // printf("finish sending response to %d with length %ld\n", ev.data.fd, nbytes);
                    control_epoll_event(fd, EPOLL_CTL_MOD, ev.data.fd, EPOLLIN | EPOLLET);
                }
                
            } else { // somthing unexpected
                control_epoll_event(fd, EPOLL_CTL_DEL, ev.data.fd, 0);
                close(ev.data.fd);
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
        epolls[i].events = calloc(MAXEVENTS, sizeof(struct epoll_event));
    }

    pthread_t th[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(&th[i], NULL, &handleConnection, (void *) &epolls[i]) != 0) {
            perror("Failed to create a thread");
        }
    }
    bool active = true;
    int current_index = 0;
    while(1)
    {
        // listener
        if (!active) usleep(100);
        int new_socket;
        if ((new_socket = accept(sock, (struct sockaddr *)&addr, (socklen_t*)&addrlen)) == -1)
        {
                active = false;
                continue;
        }
        active = true;
        // distribute the connection to different thread
        set_nonblocking(new_socket);
        control_epoll_event(epolls[current_index].epoll_fd, EPOLL_CTL_ADD, new_socket, EPOLLIN | EPOLLET);
        
        current_index++;
        if (current_index == THREAD_NUM) current_index = 0;
    }
    
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join the thread");
        }
    }
    for (int i=0; i<THREAD_NUM; i++) {
        close(epolls[i].epoll_fd);
    }
    close(sock);
    return 0;
}