/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define PORT "80"

#define MAXDATASIZE 10000 // max number of bytes we can get at once 


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Extract hostname, port, and path from URL
int parse_url(const char *url, char **hostname, char **port, char **path) {
    *hostname = NULL;
    *port = NULL;
    *path = NULL;

    // Copy url to a modifiable string
    char *url_copy = strdup(url);
    if (!url_copy) return -1; // Memory allocation failed

    if (strncmp(url_copy, "http://", 7) != 0) {
        free(url_copy);
        return -1; // Unsupported protocol
    }

    char *host_start = url_copy + 7;
    char *host_end = strchr(host_start, '/');
    if (!host_end) {
        // Assume the entire URL after "http://" is the hostname, and path is "/"
        *hostname = strdup(host_start);
        *path = strdup("/");
    } else {
        *host_end = '\0'; // Split the string
        *hostname = strdup(host_start);
        *path = strdup(host_end + 1);
    }

    if (!*hostname || !*path) {
        free(url_copy);
        return -1; // Memory allocation failed
    }

    // Check for port in hostname
    char *port_sep = strchr(*hostname, ':');
    if (port_sep) {
        *port_sep = '\0'; // Split the hostname and port
        *port = strdup(port_sep + 1);
        if (!*port) {
            free(url_copy);
            return -1; // Memory allocation failed
        }
    } else {
        *port = strdup(PORT);
    }

    free(url_copy);
    return 0;
}

int main(int argc, char *argv[])
{
    int sockfd, numbytes;  
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        fprintf(stderr,"usage: ./client http://hostname[:port]/path\n");
        exit(1);
    }

    char *hostname, *port, *path;
    if (parse_url(argv[1], &hostname, &port, &path) != 0) {
        fprintf(stderr, "Error parsing URL\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4; AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    // Send the HTTP GET request
    snprintf(buf, sizeof(buf), "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, hostname);
    if (send(sockfd, buf, strlen(buf), 0) == -1) {
        perror("send");
        exit(1);
    }

    FILE *file = fopen("output", "wb");
    if (!file) {
        perror("fopen");
        exit(1);
    }

    int header_ended = 0;

    while ((numbytes = recv(sockfd, buf, MAXDATASIZE - 1, 0)) > 0) {
        // Find the end of the header once
        if (!header_ended) {
            buf[numbytes] = '\0'; // Null-terminate for string search
            char *header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                header_ended = 1;
                header_end += 4; // Move past the header
                int header_length = header_end - buf;
                fwrite(header_end, numbytes - header_length, 1, file); // Write the rest after the header
            }
        } else {
            fwrite(buf, numbytes, 1, file); // Write directly as we are past headers
        }
    }

    if (numbytes == -1) {
        perror("recv");
        exit(1);
    }

    fclose(file);
    close(sockfd);
    free(hostname);
    free(port);
    free(path);

    printf("File received and saved to 'output'\n");

    return 0;
}

