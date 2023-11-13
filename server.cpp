// Server side C program to demonstrate Socket programming
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <errno.h>
#include <poll.h>


//#include <fstream>
//#include <sstream>
//#include <arpa/inet.h>
//#include <string>
//#include <netdb.h>
//#include <unistd.h>
//using namespace std;

#define PORT 8081

char* parse(char line[], const char symbol[]);
char* parse_method(char line[], const char symbol[]);
char* find_token(char line[], const char symbol[], const char match[]);
int send_message(int fd, char image_path[], char head[]);
//void setHttpHeader_other(char httpHeader[], char *path);
//void setHttpHeader(char httpHeader[]);
//void report(struct sockaddr_in *serverAddress);
/*std::string read_image(const std::string& image_path);
int send_image(int & fd, std::string& image);*/

//https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
//https://stackoverflow.com/questions/45670369/c-web-server-image-not-showing-up-on-browser
//https://www.linuxhowtos.org/C_C++/socket.htm

char http_header[25] = "HTTP/1.1 200 Ok\r\n";


int main(int argc, char const *argv[])
{
    int server_fd, new_socket, pid; 
    long valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("In sockets");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( PORT );
    
    memset(address.sin_zero, '\0', sizeof address.sin_zero);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0)
    {
        perror("In bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0)
    {
        perror("In listen");
        exit(EXIT_FAILURE);
    }
    
    //report(&address);
    //setHttpHeader(httpHeader);
    
    while(1)
    {
        printf("\n+++++++ Waiting for new connection ++++++++\n\n");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
        {
            perror("In accept");
            exit(EXIT_FAILURE);
        }
        //Create child process to handle request from different client
        pid = fork();
        if(pid < 0){
            perror("Error on fork");
            exit(EXIT_FAILURE);
        }
        
        if(pid == 0){
            if (send(new_socket, "Hello world\n", 13, 0) < 0) {
                perror("send failed!");
                exit(EXIT_FAILURE);
            }
            close(new_socket);
        }
        else{
            printf(">>>>>>>>>>Parent create child with pid: %d <<<<<<<<<", pid);
            close(new_socket);
        }
    }
    close(server_fd);
    return 0;
}

