#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * client.c: A very, very primitive HTTP client.
 * 
 * To run, try: 
 *      ./client www.cs.technion.ac.il 80 /
 *
 * Sends one HTTP request to the specified HTTP server.
 * Prints out the HTTP response.
 *
 * HW3: For testing your server, you will want to modify this client.  
 * For example:
 * 
 * You may want to make this multi-threaded so that you can 
 * send many requests simultaneously to the server.
 *
 * You may also want to be able to request different URIs; 
 * you may want to get more URIs from the command line 
 * or read the list from a file. 
 *
 * When we test your server, we will be using modifications to this client.
 *
 */

#include "segel.h"

/*
 * Send an HTTP request for the specified file 
 */
void clientSend(int fd, char *filename)
{
  char buf[MAXLINE];
  char hostname[MAXLINE];

  Gethostname(hostname, MAXLINE);

  /* Form and send the HTTP request */
  sprintf(buf, "GET %s HTTP/1.1\n", filename);
  sprintf(buf, "%shost: %s\n\r\n", buf, hostname);
  Rio_writen(fd, buf, strlen(buf));
}
  
/*
 * Read the HTTP response and print it out
 */
void clientPrint(int fd)
{
  rio_t rio;
  char buf[MAXBUF];  
  int length = 0;
  int n;
  
  Rio_readinitb(&rio, fd);

  /* Read and display the HTTP Header */
  n = Rio_readlineb(&rio, buf, MAXBUF);
  while (strcmp(buf, "\r\n") && (n > 0)) {
    printf("Header: %s", buf);
    n = Rio_readlineb(&rio, buf, MAXBUF);

    /* If you want to look for certain HTTP tags... */
    if (sscanf(buf, "Content-Length: %d ", &length) == 1) {
      printf("Length = %d\n", length);
    }
  }

  /* Read and display the HTTP Body */
  n = Rio_readlineb(&rio, buf, MAXBUF);
  while (n > 0) {
    printf("%s", buf);
    n = Rio_readlineb(&rio, buf, MAXBUF);
  }
}

void* worker_routine(void* args){
  char** argv = (char**)args;
  int argc = atoi(argv[0]);
  char *host, *filename;
  int port;
  int clientfd;

  //   if (argc != 4) {
  //   fprintf(stderr, "Usage: %s <host> <port> <filename>\n", argv[0]);
  //   exit(1);
  // }

  // host = argv[1];
  // port = atoi(argv[2]);
  // filename = argv[3];
  host = "localhost";
  port = 6666;
  filename = "home.html";
  fprintf(stderr, "brrrrrr");
  /* Open a single connection to the specified host and port */
  clientfd = Open_clientfd(host, port);
  
  clientSend(clientfd, filename);
  clientPrint(clientfd);
    
  Close(clientfd);
  exit(0);
}

int create_worker_threads(int argc, char *argv[])
{
  int num_threads = 5;
  pthread_t *threads = malloc((num_threads+10)*sizeof(pthread_t));
  for (size_t i = 0; i < num_threads; i++)
  {
      pthread_create(&threads[i], NULL, worker_routine, argv);
  }
  return 0;
}

int main(int argc, char *argv[])
{
  create_worker_threads(argc, argv);
  exit(0);
}
