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

void error(char *msg)
{
  perror(msg);
  exit(1);
}

int main(void)
{
  int sockfd, new_fd, portno, pid;
  socklen_t clilen;
  struct sockaddr_in serv_addr, cli_addr;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);	//create socket
  if (sockfd < 0) error("ERROR opening socket");
  memset((char *) &serv_addr, 0, sizeof(serv_addr));	//reset memory

  // Set socket reusable to true to avoid "Address already in use" error
  int optval;
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

  //fill in address info

  // Set default port number here.
  // Set to 1738 for this lab assignment / can be changed.
  portno = 1738;

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    error("ERROR on binding");

  listen(sockfd,5);	//5 simultaneous connection at most

  printf("Server waiting for connections requests...\n\n");

  // Continously accept and handle requests until closed from the console.
  while (1) {
    //accept connections
    new_fd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (new_fd < 0) error("ERROR on accept");

    // Fork off separate child process to handle each client request
    if (!fork())
    {
      // Child process doesn't listener
      close(sockfd);

      // Print to console
      char buffer[512];
      memset(buffer, '\0', 512);
      int x = read(new_fd, buffer, 511);  // leave one final character for the nullbyte

      if (x < 0) perror("Socket Reading Failed!");

      printf("<<< HTTP REQUEST MESSAGE >>>\n%s\n", buffer);

      char* filename;
      filename = strtok(buffer, " ");
      filename = strtok(NULL, " ");	// grabs second token that contains the file name with a leading slash
      filename++;	// we don't want the leading slash
      //printf(filename);

      if (strlen(filename) <= 0)  // if no file is requested
      {
        send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
        send(new_fd, "<html><head></head><body><h1>404 Not Found</h1><p>No file specified!</p></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1><p>No file specified!</p></body></html>\r\n"), 0);
        printf("ERROR: No file specified!\n\n");
        goto CLOSE_CONNECTION;
      }

      // Open file
      FILE *filep = fopen(filename, "r");

      if (!filep) // file not found in directory
      {
        send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
        send(new_fd, "<html><head></head><body><h1>404 Not Found</h1><p>No file found!</p></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1><p>No file found!</p></body></html>\r\n"), 0);
        printf("ERROR: File not found!\n\n");
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
        printf("ERROR: File allocation error!\n\n");
        goto CLOSE_CONNECTION;
      }

      // Copy file into buffer
      size_t flen = fread(f, 1, flsize, filep);

      if (flen != flsize)  // check no reading error has ocurred.
      {
        send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"), 0);
        send(new_fd, "<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n", strlen("<html><head></head><body><h1>404 Not Found</h1></body></html>\r\n"), 0);
        printf("ERROR: File reading error!\n\n");
        goto CLOSE_CONNECTION;
      }

      // Null terminating the file buffer
      f[flen] = '\0';

      // Generate HTTP Response Message : the format is taken from Chapter 2 in textbook pg. 105-106
      /*
        HTTP/1.1 200 OK
        Connection: close
        Date: Tue, 09 Aug 2011 15:44:04 GMT
        Server: Apache/2.2.3 (CentOS)
        Last-Modified: Tue, 09 Aug 2011 15:11:03 GMT
        Content-Length: 6821
        Content-Type: text/html
      */

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
      strcat(contentlength, "\r\n");

      memcpy(msg+offset, contentlength, strlen(contentlength));
      offset += strlen(contentlength);

      // content type
      char* ctype;
      ctype = "Content-Type: text/plain\r\n";  // assume it is text type intially
      char* ftype;
      char tmp[50];
      strcpy(tmp, filename);
      ftype = strtok(tmp, ".");
      ftype = strtok(NULL, ".");

      if (strcmp(ftype, "html") == 0)
        ctype = "Content-Type: text/html\r\n";
      else if (strcmp(ftype, "jpg") == 0 || strcmp(ftype, "jpeg") == 0)
        ctype = "Content-Type: image/jpg\r\n";
      else if (strcmp(ftype, "gif") == 0)
        ctype = "Content-Type: image/gif\r\n";

      memcpy(msg+offset, ctype, strlen(ctype));
      offset+=strlen(ctype);
      memcpy(msg+offset, "\r\n\0", 3);

      // print response to console
      printf("<<< HTTP RESPONSE MESSAGE >>>\n%s\n", msg);

      // send response to client as header
      send(new_fd, msg, strlen(msg), 0);

      // send file to client browser
      send(new_fd, f, flen, 0);

      printf("SUCCESS: File '%s' served to client!\n\n", filename);

      // Free up file pointer resources.
      fclose(filep);
      free(f);

      CLOSE_CONNECTION:
        close(new_fd);
        exit(0);
    }
    close(new_fd);
  }
  return 0;
}
