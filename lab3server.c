#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "lab3avl.h"

#define BUFSIZE 1024
#define FILENAME_SIZE 20
#define LISTENBACKLOG 50
#define MAX_EPOLL_EVENTS 64
#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

typedef struct {
	FILE * fp; // input file 
	int cfd; // client socket
	int nBytes; // number of bytes in file
	int bytesSortedContent;
	int assigned;
	int sorted;
	int sent;
	char* line; // current line
	size_t capacity;
	size_t length;
} filecontext;

const int argnum = 3;
FILE *f_output = NULL;

int checkfile (char *name) {
	FILE *fp = NULL;
	fp = fopen(name,"r");
	if (fp == NULL) {
		return 1;
	}
		
	fclose(fp);
	return 0;
}

void writeOutput(int key, char* line) {
	size_t nToWrite = strlen(line);
	int fd = fileno(f_output);
	char* pCursor = line;
	while (nToWrite > 0) {
		size_t nWritten = write(fd, pCursor, nToWrite);
		if (nWritten < 0) {
			handle_error("write failed");
		}
		pCursor += nWritten; // advance cursor
		nToWrite -= nWritten;
	}
}

int 
getIPAddress(char **ipaddress) {
	struct ifaddrs *ifaddr = NULL;
	char buf[64] = "";

	if (getifaddrs(&ifaddr) == -1) {
		handle_error("getifaddrs");
	}

	for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) { 
		if (ifa->ifa_addr == NULL)
			continue;
		//skip loopback interface - 127.0.0.1
		if ((ifa->ifa_flags & IFF_LOOPBACK))
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			void *inaddr = NULL;
			struct sockaddr_in *s4 = (struct sockaddr_in*)ifa->ifa_addr;
			inaddr = &s4->sin_addr;

			if (inet_ntop(ifa->ifa_addr->sa_family, inaddr, buf, sizeof(buf)) != 0) {
				break;
			}
		}
	}
	if (ifaddr != NULL) {
		freeifaddrs(ifaddr);
	}
	if (strlen(buf) > 0) {
		*ipaddress = strdup(buf);
		return 0;
	}
	return 1; 
}


int 
main (int argc, char *argv[]) {
	int epfd, event_num, sfd, i;
	int linenum = 0;
	int nFiles = 0;
	int flag = 0;
	struct sockaddr_in my_addr, peer_addr;
	struct epoll_event event = {0};
	socklen_t peer_addr_size;
	char *filename = NULL;
	int portnumber = -1;
	struct epoll_event captured[MAX_EPOLL_EVENTS];
	struct Node* root = NULL;
	char *ipaddress = NULL;	

	
	filecontext * filecontexts = NULL;
	FILE *f = NULL;

	if (argc < argnum) {
		printf("Usage: ./lab3server <filename> <portnum>\n");
		errno = EINVAL;
		handle_error("invalid command line arguments");
	}
		
	filename = argv[1];
	portnumber = atoi(argv[2]);

	if (portnumber < 0) {
		errno = EINVAL;
		handle_error("invalid port number");
	}
	
	//check if file can be opened	
	if (checkfile(filename) != 0) {
		errno = EINVAL;
		handle_error("invalid file name");
	}

	//open file 
	f = fopen(filename, "r");
	if (f == NULL) {
		errno = EINVAL;
		handle_error("failed to open file");
	}

	if (getIPAddress(&ipaddress) != 0 ) {
		handle_error("getIPAddress");
	}

	printf("IP Address: %s, Port Number: %d\n", ipaddress, portnumber);

	while (!feof(f)) {
		char buf[1024];
		int ret_fscanf;
		ret_fscanf = fscanf(f, "%s\n", buf);
		if (ret_fscanf < 1) {
		       errno = EINVAL;
		       handle_error("fscanf");
		}
		//remove newline 
		buf[strlen(buf)] = '\0';
		if (linenum == 0) {
			//first line is output file
			f_output = fopen(buf,"w");
			if (f_output == NULL) {
				handle_error("fopen");
			}
		} else { //input file
		       if (checkfile(buf) != 0) {
		       		errno = EINVAL;
		 		handle_error("failed to open input file");
		       }
		       nFiles++; // number of fragments
		}
		linenum++;
	}		
	fclose(f);
	f = NULL;	

	if (nFiles == 0) {
		handle_error("invalid number of files to allocate");
	}

	filecontexts = (filecontext *) malloc(nFiles * sizeof(filecontext)); 
	
	if (filecontexts == NULL) {
		handle_error("failed to allocate file contexts");
	}

	//instantiate filecontexts struct for input files
	for (i = 0; i < nFiles; i++) {
		filecontexts[i].fp = NULL;
		filecontexts[i].cfd = -1;
		filecontexts[i].nBytes = 0;
		filecontexts[i].bytesSortedContent = 0;
		filecontexts[i].sorted = 0;
		filecontexts[i].sent = 0;
		filecontexts[i].assigned = 0;
		filecontexts[i].line = NULL;
		filecontexts[i].capacity = 0;
		filecontexts[i].length = 0;
	}

	//open file 
	f = fopen(filename, "r");
	if (f == NULL) {
		errno = EINVAL;
		handle_error("failed to open file");
	}
	
	linenum = 0;

	while (!feof(f)) {
		char buf[1024];
		int ret_fscanf;
		ret_fscanf = fscanf(f, "%s\n", buf);
		if (ret_fscanf < 1) {
		       errno = EINVAL;
		       handle_error("fscanf");
		}
		//remove newline 
		buf[strlen(buf)] = '\0';
		if (linenum != 0) { //input file
			filecontexts[linenum-1].fp = fopen(buf,"r");
			if (filecontexts[linenum-1].fp == NULL) {
				handle_error("invalid input file");
			}
		}
		linenum++;
	}		
	fclose(f);
	f = NULL;	

        //open socket
	if ( (sfd = socket(AF_INET, SOCK_STREAM,0)) == -1)
		handle_error("socket");

	memset(&my_addr, 0, sizeof(struct sockaddr_in));

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(portnumber);
	my_addr.sin_addr.s_addr = INADDR_ANY;

	int reuse = 1; 
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	if (bind(sfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr_in)) == -1)
		handle_error("bind");
	// printf("bind\n");

	if (listen(sfd, LISTENBACKLOG) == -1)
		handle_error("listen");
	// printf("listen\n");

	peer_addr_size = sizeof(struct sockaddr_in);

	epfd = epoll_create1(0);

	if(epfd == -1)
		handle_error("epoll_create1");

	event.data.fd = sfd;
	event.events = EPOLLIN; 

	int ret_ctl;
	if ((ret_ctl = epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &event)) == -1)
		handle_error("epoll_ctl");
	
	while (!flag)
	{
		int i = 0;
		if ((event_num = epoll_wait(epfd, captured, MAX_EPOLL_EVENTS, 5)) == -1)
		{
			handle_error("epoll_wait");
		}
		if (event_num == 0) { // timed out
		   int numSorted = 0;
		   for (i = 0; i < nFiles; i++) {
		        int ret;
			char buf[BUFSIZE + 2];
			if (filecontexts[i].sorted == 1) {
				numSorted++;
				continue;
			}
			if (filecontexts[i].cfd >= 0 && filecontexts[i].fp != NULL && !filecontexts[i].sent) {
				int eof = 0;
				if(!feof(filecontexts[i].fp)) {
					memset(buf,0,sizeof(buf));
					if ((fgets(buf,BUFSIZE, filecontexts[i].fp) == NULL)) {
						if (!feof(filecontexts[i].fp)) {
							handle_error("read from file");
						} else {
							eof = 1;
						}
					}

					if (strlen(buf) > 0) {
						if (buf[strlen(buf) - 1] != '\n') {
					        	buf[strlen(buf) - 1] = '\n';
						}	       
						// save length
						filecontexts[i].nBytes += strlen(buf);
						// send line to client
						// printf("Writing %d bytes to client: %d\n", strlen(buf), filecontexts[i].cfd);
						ret = write(filecontexts[i].cfd,buf,strlen(buf));
						if (ret < 0) {
							handle_error("write");
						}
					}
				} else {
					eof = 1;
				}
				if (eof) {
					// Tell client we are done sending the content
					// And we are ready for sorted content
					ret = sprintf(buf, "-1\n");
					if (ret < 0) {
						handle_error("sprintf");
					}
					ret = write(filecontexts[i].cfd,buf,strlen(buf));
					if (ret < 0) {
						handle_error("write");
					}
					filecontexts[i].sent = 1;
				}
			}
		   }
		   if (numSorted == nFiles) {
			   // traverse the avl tree in order and write to out file
			   inOrder(root, writeOutput);
			   fclose(f_output);
			   f_output = NULL;
			   flag = 1; // done
		   }
		   // printf("%d sorted of %d\n", numSorted, nFiles);
		}
		for (; i < event_num; i++)
		{
			if (captured[i].data.fd == sfd) { 
				if (captured[i].events & EPOLLIN) {
					int j = 0;
			 		filecontext *p = NULL;
					int cfd = accept(sfd, (struct sockaddr *) &peer_addr, &peer_addr_size);

					if (cfd == -1)
						handle_error("accept");

					printf("accepted connection - %d\n", cfd);

					for (;j < nFiles;j++) {
						if (!filecontexts[j].assigned) {
							p = &filecontexts[j];
							break;
						}
					}
					if (p == NULL) { // everything assigned
						close(cfd);
					} else {
						struct epoll_event cevent = {0};
						p->cfd = cfd;
						p->assigned = 1;

						cevent.data.fd = cfd;
						cevent.events = EPOLLIN | EPOLLRDHUP;

						if ((ret_ctl = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cevent)) == -1)
							handle_error("epoll_ctl");
					}
				}
			} else { //client 
				int k;
			 	filecontext *p = NULL;
				int fd = captured[i].data.fd;
				//printf("Reading from client\n");
				for (k = 0; k < nFiles; k++) {
					if (fd	== filecontexts[k].cfd) {
						p = &filecontexts[k];
						break;
					}
				}
				if (p == NULL) {
					continue;
				}
				if (captured[i].events & EPOLLRDHUP) {
 					if ((ret_ctl = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &event)) == -1)
						handle_error("epoll_ctl");

					if (close(fd) < 0)
						handle_error("close");

					p->cfd = -1;
				} else if (captured[i].events & EPOLLIN) {
					char buf[BUFSIZE];
					int numbytes = 0;
					size_t available = p->capacity - p->length;
					numbytes = read(fd, buf, BUFSIZE);
					if (numbytes < 0) {
						handle_error("read failed");
					}
					for (int y = 0; y < numbytes; y++) {
						int line;
						char ch = buf[y];
						if (p->line == NULL) {
							p->line = (char*)malloc(BUFSIZE);
							p->capacity = BUFSIZE;
							p->length = 0;
							available = BUFSIZE;
						}
						// save ch in line
						if (available == 0) {
							p->line = (char*)realloc(p->line, p->capacity + BUFSIZE);
							p->capacity += BUFSIZE;
							available += BUFSIZE;
						}
						p->line[p->length++] = ch;
						available--;
						if (ch == '\n') {
							// null terminate line
							if (available == 0) {
								p->line = (char*)realloc(p->line, p->capacity + BUFSIZE);
								p->capacity += BUFSIZE;
								available += BUFSIZE;
							}
							p->line[p->length++] = '\0';
							available--;
							// get line number for key
							sscanf(p->line, "%d", &line);

							// printf("insert node: %d, %s\n", line, p->line);
					         	root = insertNode(root, line, p->line);
							p->bytesSortedContent += strlen(p->line);
							// reset the line
							available += p->length;
							p->length = 0;
						}
					}
					if (p->nBytes == p->bytesSortedContent) {
						struct epoll_event cevent = {0};

						cevent.data.fd = fd;
						cevent.events = EPOLLIN | EPOLLRDHUP;

						// We are done. Remove from epoll queue
 					        if ((ret_ctl = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &cevent)) == -1)
							handle_error("epoll_ctl");
						p->sorted = 1;
						close(p->cfd); // close the client socket
						p->cfd = -1;
					}
				}
			}
		}
	}

	if (epfd >= 0)
	{
		if (close(epfd))
			handle_error("close");

	}
	if (filecontexts) {
		int z = 0;
		for (z = 0; z < nFiles; z++) {
			if (filecontexts[z].line != NULL) {
				free(filecontexts[z].line);
			}
			if (filecontexts[z].cfd >= 0) {
				close(filecontexts[z].cfd);
			}
		}
		free(filecontexts);
	}
	if (root) {
		deleteTree(root);
	}
	if (ipaddress) {
		free(ipaddress);
	}

	return 0;
}
