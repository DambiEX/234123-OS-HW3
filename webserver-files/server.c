#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST -1
#define END_OF_BUFFER -2
int *incoming_requests_buffer, *handled_requests_buffer;
int incoming_i, handled_i;
int incoming_size, handled_size;
pthread_mutex_t incoming_lock, handled_lock;
pthread_cond_t incoming_cond, handled_cond;


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

initialize_buffer(int size, int* buffer)
{
    buffer[size] = END_OF_BUFFER;
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = NULL_REQUEST;
    }
}

initialize_buffers(int queue_size, int num_threads)
{
    incoming_size = 0;
    handled_size = 0;
    incoming_requests_buffer = malloc(queue_size+SAFETY_MARGIN);
    initialize_buffer(queue_size, incoming_requests_buffer);
    handled_requests_buffer = malloc(num_threads+SAFETY_MARGIN);
    initialize_buffer(num_threads, handled_requests_buffer);
}

initialize_locks()
{
    pthread_mutex_init(&incoming_lock, NULL);
    pthread_mutex_init(&handled_lock, NULL);
    pthread_cond_init(&incoming_cond, NULL);
    pthread_cond_init(&handled_cond, NULL);
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

void add_to_incoming_buffer(int connfd, int queue_size, void (*sched_func)())
{
    //only add request if there is room in incoming buffer
    pthread_mutex_lock(&incoming_lock);
    pthread_mutex_lock(&handled_lock); //okay to lock since no chance of deadlock, and we want updated data
    if (incoming_i + handled_i >= queue_size) 
    {
        pthread_mutex_unlock(&handled_lock);
        sched_func();
        return;
    }
    pthread_mutex_unlock(&handled_lock);
    incoming_i = (incoming_i+1)%queue_size; //cyclic buffer since we are removing the oldest request every time
    incoming_size++;
    incoming_requests_buffer[incoming_i] = connfd;
    pthread_cond_signal(&incoming_cond);
    pthread_mutex_unlock(&incoming_lock);    
}

wait_for_request()
{
    int connfd;
    pthread_mutex_lock(&incoming_lock);
        while (incoming_i == 0) //does not need to check handled since we know this thread is not busy 
        {
            pthread_cond_wait(&incoming_cond, &incoming_lock);
        }
        pthread_mutex_lock(&handled_lock);
            connfd = incoming_requests_buffer[incoming_i];
            handled_i++;
            handled_requests_buffer[handled_i] = connfd;
            incoming_i--;
        pthread_mutex_unlock(&handled_lock);
    pthread_mutex_unlock(&incoming_lock);
}

int main(int argc, char *argv[])
{
    int port, num_threads, queue_size, listenfd, connfd, clientlen;
    char sched_alg[ARG_MAX_LEN];
    struct sockaddr_in clientaddr;
    void* sched_func;
    
    getargs(&port, &num_threads, &queue_size, &sched_alg, argc, argv);
    initialize_buffers(queue_size,num_threads);
    initialize_locks();
    create_worker_threads(port, num_threads, queue_size, argv);

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen); //should stay in main thread
    add_to_incoming_buffer(connfd, queue_size, sched_func);
	// 
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads 
	// do the work. 
	// 
	// requestHandle(connfd);

	// Close(connfd);
    }
    free (incoming_requests_buffer);
}


    


 
