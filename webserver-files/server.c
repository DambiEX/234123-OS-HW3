#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST -1
#define END_OF_BUFFER -2
int *requests_buffer, buf_end, buf_start, buf_size, handled_reqs_num;
pthread_mutex_t lock;
pthread_cond_t cond;


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

init_global_vars(int queue_size)
{
    buf_start = 0;
    buf_end = 0;
    buf_size = 0;
    handled_reqs_num = 0;
    requests_buffer = malloc(queue_size + SAFETY_MARGIN);
    initialize_buffer(queue_size, requests_buffer);
}

init_lock()
{
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
}

void worker_routine(int queue_size)
{
    int connfd;
    while (1){
        connfd = pop_buffer(queue_size);
        requestHandle(connfd);
        Close(connfd);
        pthread_mutex_lock(&lock);
        handled_reqs_num--;
        pthread_mutex_unlock(&lock);
    }
}

int create_worker_threads(int num_threads, int queue_size)
{
    pthread_t *threads;
    for (size_t i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_routine, queue_size);
    }
    return 0;
}

void push_buffer(int connfd, int queue_size, void (*sched_func)())
{
    pthread_mutex_lock(&lock);
    if (buf_size + handled_reqs_num >= queue_size)  // only add request if there is room in buffer
    {
        sched_func();
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
        return;
    }
    buf_end = (buf_end + 1) % queue_size; // cyclic queue since we are removing the oldest request every time
    requests_buffer[buf_end] = connfd; // push to cyclic queue
    buf_size++;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);    
}

pop_buffer(queue_size)
{
    int connfd;
    pthread_mutex_lock(&lock);
    while (buf_size == 0)
    {
        pthread_cond_wait(&cond, &lock);
    }
    buf_start = (buf_start + 1) % queue_size; 
    connfd = requests_buffer[buf_start]; // remove from start of cyclic queue
    buf_size--;
    handled_reqs_num++;
    pthread_mutex_unlock(&lock);
    return connfd;
}

int main(int argc, char *argv[])
{
    int port, num_threads, queue_size, listenfd, connfd, clientlen;
    char sched_alg[ARG_MAX_LEN];
    struct sockaddr_in clientaddr;
    void* sched_func;
    
    getargs(&port, &num_threads, &queue_size, &sched_alg, argc, argv);
    init_global_vars(queue_size);
    init_lock();
    create_worker_threads(num_threads, queue_size);

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen); //should stay in main thread
    push_buffer(connfd, queue_size, sched_func);
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


    


 
