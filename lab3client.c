#include <stdio.h>  
#include <stdlib.h> 
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/un.h> 
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "lab3avl.h"
#include <sys/epoll.h>

#define DES_SOCKET_PATH "/home/pi/Socket"
#define MAX_EPOLL_EVENTS 2
#define BUFSIZE 1024
#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

const int arg_num = 3;
int sfd = -1;

void processLines(int key, char* line) {
	if (sfd > 0) {
		int nWritten = write(sfd, line, strlen(line));
		if (nWritten < 0) {
			handle_error("write to server failed");
		}
	}
}

int main( int argc, char* argv[] ) {
    int port, event_num, epfd;
    struct sockaddr_in my_addr;
    // char msg[BUFSIZE+2]; // extra for newline and null termination
    char *ip;
    int flag = 0;
    struct epoll_event event, captured[MAX_EPOLL_EVENTS];
    struct Node *root = NULL;
    char* line = NULL;
    size_t length = 0;
    char  buf[BUFSIZE];
    size_t capacity = 0; 

    if (argc != arg_num) {
        printf("Usage: ./client <ip address>  <port number>\n");
        return -1;
    }

    ip = argv[1];

    port = atoi(argv[2]);
    if (port < 0) {
	    handle_error("invalid port number");
    }

    //create socket
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
      handle_error("socket");

    //clear struct, ensure struct fields = 0
    memset(&my_addr, 0, sizeof(struct sockaddr_in));

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    if (inet_aton(ip, &(my_addr.sin_addr)) == 0)
      handle_error("inet_aton");

    if (connect(sfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_in)) == -1)
      handle_error("connect");

    epfd = epoll_create1(0);

    if(epfd == -1) {
	    handle_error("epoll_create1");
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLRDHUP;

    //add input event to interest list 
    int ret_ctl;
    // int line; 

    ret_ctl = epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &event);
    if (ret_ctl == -1) {
	    handle_error("epoll_ctl");
    }
    
	while (!flag)
	{
		if ((event_num = epoll_wait(epfd, captured, MAX_EPOLL_EVENTS, -1)) == -1)
		{
			handle_error("epoll_wait");
		} 
		
		if (event_num > 0) { //read
			if (captured[0].events & EPOLLRDHUP) {
				if ((ret_ctl = epoll_ctl(epfd, EPOLL_CTL_DEL, sfd, &event)) == -1) {
					handle_error("epoll_ctl");
				}						
				flag = 1;
			} else if (captured[0].events & EPOLLIN) {
				size_t available = capacity - length;
				size_t numbytes = read(captured[0].data.fd, buf, BUFSIZE);
				if (numbytes < 0) {
					handle_error("read failed");
				}
				for (int y = 0; y < numbytes; y++) {
					int linenum;
					char ch = buf[y];
					if (line == NULL) {
						line = (char*)malloc(BUFSIZE);
						capacity = BUFSIZE;
						length = 0;
						available = BUFSIZE;
					}
					// save ch in line
					if (available == 0) {
						line = (char*)realloc(line, capacity + BUFSIZE);
						capacity += BUFSIZE;
						available += BUFSIZE;
					}
					line[length++] = ch;
					available--;
					if (ch == '\n') {
						// null terminate line
						if (available == 0) {
							line = (char*)realloc(line, capacity + BUFSIZE);
							capacity += BUFSIZE;
							available += BUFSIZE;
						}
						line[length++] = '\0';
						available--;
						sscanf(line, "%d", &linenum);
						if (linenum == -1) {
							inOrder(root, &processLines);
						} else {
							root = insertNode(root, linenum, line);
						}
						// reset line
						available += length;
						length = 0; 
					}
				}
			}
		}
	}

	if (epfd >= 0) {
		if (close(epfd)) {
			handle_error("close");
		}
	}
	if (root != NULL) {
		deleteTree(root);
	}

	return 0;
}

