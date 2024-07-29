/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10	 // how many pending connections queue will hold
#define MAXDATASIZE 4096 // max number of bytes we can get at once 

void sigchld_handler(int s)
{
	s++;
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	if (argc != 2) {
        fprintf(stderr,"usage: server port\n");
        exit(1);
    }

	char *port = argv[1]; // Use the port provided as a command-line argument

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}


	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections on port %s...\n", port);

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            char buf[MAXDATASIZE];
            int numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0);
            if (numbytes == -1) {
                perror("recv");
                exit(1);
            }

            buf[numbytes] = '\0'; // null-terminate the buffer

            // Simple parsing of the GET request
            char *method = strtok(buf, " ");
            char *path = strtok(NULL, " ");
            char *protocol = strtok(NULL, "\r\n");

            if (method && path && protocol && strcmp(method, "GET") == 0) {
                // Adjust path for local file system
                if (path[0] == '/') path++; // Remove leading slash for local file system compatibility

                FILE *fp = fopen(path, "rb");
                if (fp) {
                    char header[] = "HTTP/1.1 200 OK\r\n\r\n";
                    send(new_fd, header, strlen(header), 0);

                    // Read file and send its contents
                    while ((numbytes = fread(buf, 1, MAXDATASIZE, fp)) > 0) {
                        send(new_fd, buf, numbytes, 0);
                    }
                    fclose(fp);

					// Send a newline character at the end of the response
					// char newline[] = "\n";
					// send(new_fd, newline, strlen(newline), 0);
                } else {
                    char header[] = "HTTP/1.1 404 Not Found\r\n\r\n";
                    send(new_fd, header, strlen(header), 0);
                }
            } else {
                char header[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
                send(new_fd, header, strlen(header), 0);
            }
            close(new_fd);
            exit(0);
        }
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}