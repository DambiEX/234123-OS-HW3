#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST -1
#define END_OF_BUFFER -2
int *requests_buffer;
pthread_cond_t requests_buffer_cond;


// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

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

void worker_routine()
{
    int connfd;
    while (1){
        connfd = wait_for_request();
        requestHandle(connfd);
        Close(connfd);
    }
}

int create_worker_threads(int port, int num_threads, int queue_size, char *argv)
{
    pthread_t *threads;
    for (size_t i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_routine, argv);
    }
    return 0;
}

initialize_buffer(int size, int* buffer)
{
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = NULL_REQUEST;
    }
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, clientlen;
    struct sockaddr_in clientaddr;

    int port, num_threads, queue_size; //args
    char sched_alg[ARG_MAX_LEN];
    getargs(&port, &num_threads, &queue_size, &sched_alg, argc, argv);

    requests_buffer = malloc(queue_size+SAFETY_MARGIN);
    initialize_buffer(queue_size, requests_buffer);
    create_worker_threads(port, num_threads, queue_size, argv);

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

	// 
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads 
	// do the work. 
	// 
	// requestHandle(connfd);

	// Close(connfd);
    }
    free (requests_buffer);
}


    


 
