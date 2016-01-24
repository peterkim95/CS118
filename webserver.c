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
#include <time.h>  // for easier time formatting for response header
#include <sys/stat.h>

#define PORT "1738"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
      return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
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

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

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

    printf("server: waiting for connections...\n");

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

						// Print to console
						char buffer[512];
						memset(buffer, '\0', 512);
						int x = read(new_fd, buffer, 511);

						if (x < 0)
              perror("Socket Reading Failed!");

            printf("<<< HTTP REQUEST MESSAGE >>>\n%s\n", buffer);

            char* filename;
            const char spc[2] = " ";
            filename = strtok(buffer, spc);
            filename = strtok(NULL, spc);	// grabs second token that contains the file name with a leading slash
            filename++;	// we don't want the leading slash

            if (strlen(filename) <= 0)  // if no file is requested
              filename = "no*file";

						// Serve the requested file
            if (filename == "no*file")
            {
              send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
              send(new_fd, "<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n"), 0);
              printf("ERROR: No file specified!\n");
              goto CLOSE_CONNECTION;
            }

            // Open file
            FILE *filep = fopen(filename, "r");

            if (!filep) // file not found in directory
            {
              send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
              send(new_fd, "<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n"), 0);
              printf("ERROR: File not found!\n");
              goto CLOSE_CONNECTION;
            }

            // obtaining the file size
            fseek (filep , 0 , SEEK_END);
            long flsize = ftell(filep);
            rewind (filep);

            // allocate enough memory to contain the whole file
            char* f;
            f = (char*) malloc(sizeof(char) * flsize);

            if (!f) // file allocation went wrong somewhere
            {
              send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
              send(new_fd, "<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n"), 0);
              printf("ERROR: File allocation error!\n");
              goto CLOSE_CONNECTION;
            }

            // Copy file into buffer
            size_t flen = fread(f, 1, flsize, filep);

            if (flen != flsize)  // check no reading error has ocurred.
            {
              send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
              send(new_fd, "<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n"), 0);
              printf("ERROR: File reading error!\n");
              goto CLOSE_CONNECTION;
            }

            // Null terminating the file buffer
            f[flen] = '\0';

            // Send appropriate HTTP response header to client

            // Header message buffer
            char msg[512];

            // Status code
            char* status;
            status = "HTTP/1.1 200 OK\r\n";

            memcpy(msg, status, strlen(status));
            int offset = strlen(status);

            // Connection status
            char* connection;
            connection = "Connection: close\r\n";

            memcpy(msg+offset, connection, strlen(connection));
            offset += strlen(connection);

            // Date & Time
            time_t rawnow;
            time(&rawnow);
            struct tm* info;
          	info = gmtime(&rawnow);
          	char date[50];
          	strftime(date, 50, "Date: %a, %d %b %Y %T %Z", info);
          	strcat(date, "\r\n");

            memcpy(msg+offset, date, strlen(date));
            offset += strlen(date);

            // Server info
            char* server;
            server = "Server: CS118Lab1Pseudo/1.0\r\n";

            memcpy(msg+offset, server, strlen(server));
            offset += strlen(server);

            // last modified
          	struct stat st;
          	stat(filename, &st); // get file info
            struct tm* lmclock;
            lmclock = gmtime(&(st.st_mtime));
          	char lm[50];
          	strftime(lm, 50, "Last-Modified: %a, %d %b %Y %T %Z", lmclock);
          	strcat(lm, "\r\n");

            memcpy(msg+offset, lm, strlen(lm));
            offset += strlen(lm);

            // content length
            char contentlength[50];
          	sprintf (contentlength, "Content-Length: %d", (unsigned int)flen);
          	strcat(contentLength, "\r\n");

            memcpy(msg+offset, contentlength, strlen(contentlength));
            offset += strlen(contentlength);

            // content type

            // print response to console
            printf("<<< HTTP RESPONSE MESSAGE >>>\n%s\n", msg);

            // send response to client as header
          	send(new_fd, msg, strlen(msg), 0);


            // send file to client browser
        		send(new_fd, f, flen, 0);

        		printf("SUCCESS: File \"%s\" served to client!\n", filename);

            // Free up file pointer resources.
            fclose(filep);
            free(f);

            CLOSE_CONNECTION: close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}
