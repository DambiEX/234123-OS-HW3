#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too
void getargs(int *port, int *num_threads, int *queue_size, char **sched_alg, int argc, char *argv[])
{
    if (argc < 2) {
	fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *num_threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *sched_alg = argv[4];
}
int create_worker_threads();
// {
//         pthread_t thread1;
//     pthread_create(&thread1, NULL, worker_routine, args);
// }

int main(int argc, char *argv[])
{
    int listenfd, connfd, clientlen;
    struct sockaddr_in clientaddr;

    int port, num_threads, queue_size; //args
    char sched_alg[ARG_MAX_LEN];
    getargs(&port, &num_threads, &queue_size, &sched_alg, argc, argv);

    // 
    // HW3: Create some threads...
    //
    create_worker_threads();


    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

	// 
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads 
	// do the work. 
	// 
	requestHandle(connfd);

	Close(connfd);
    }

}


    


 
