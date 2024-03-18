#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST -1
#define END_OF_BUFFER -2
int *requests_buffer, buf_end, buf_start, queue_size, max_queue_size, handled_reqs_num;
pthread_mutex_t buf_lock;
pthread_cond_t buf_cond;


// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

//------------------------------------------HELPER FUNCTIONS------------------------------//
int queue_is_full()
{
    return (queue_size + handled_reqs_num >= max_queue_size);
}

//--------------------------------------------INIT----------------------------------------//

void getargs(int *port, int *num_threads, int *max_q_size, char *sched_alg[], int argc, char *argv[])
{
    if (argc < 2) {
	fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *num_threads = atoi(argv[2]);
    *max_q_size = atoi(argv[3]);
    *sched_alg = argv[4];
}

void initialize_buffer(int size, int* buffer)
{
    buffer[size] = END_OF_BUFFER;
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = NULL_REQUEST;
    }
}

void init_global_vars(int max_q_size)
{
    buf_start = 0;
    buf_end = 0;
    queue_size = 0;
    handled_reqs_num = 0;
    requests_buffer = malloc(max_q_size + SAFETY_MARGIN);
    initialize_buffer(max_q_size, requests_buffer);
}

void init_buf_lock()
{
    pthread_mutex_init(&buf_lock, NULL);
    pthread_cond_init(&buf_cond, NULL);
}

void master_block_and_wait();
void* parse_sched_alg(char* sched_alg_string)
{
    return master_block_and_wait;
    // TODO: parse sched alg and choose correct algorithm for part 2 of HW.
}

//-----------------------------------------------BUFFER ACTIONS----------------------------------------//

void master_block_and_wait(pthread_cond_t cond, pthread_mutex_t lock)
{
    while (queue_is_full())
    {
        pthread_cond_wait(&cond, &lock);  // the master thread must block and wait if the queue is full
    }
    if (queue_size > 0)
    {
        pthread_cond_signal(&cond); // TODO: maybe not needed?
    }
}

void push_buffer(int connfd, void (*sched_func)(int max_size))
{
    pthread_mutex_lock(&buf_lock);
    if (queue_is_full())  // only add request if there is room in queue
    {
        sched_func(max_queue_size);
        pthread_mutex_unlock(&buf_lock); // note: unlock of unlocked mutex is undefined!
        return;
    }
    buf_end = (buf_end + 1) % max_queue_size; // cyclic queue since we are removing the oldest request every time
    requests_buffer[buf_end] = connfd; // push to cyclic queue
    queue_size++;
    pthread_cond_signal(&buf_cond);
    pthread_mutex_unlock(&buf_lock);    
}

int pop_buffer()
{
    int connfd;
    pthread_mutex_lock(&buf_lock);
    while (queue_size == 0)
    {
        pthread_cond_wait(&buf_cond, &buf_lock);
    }
    buf_start = (buf_start + 1) % max_queue_size; 
    connfd = requests_buffer[buf_start]; // remove from start of cyclic queue
    queue_size--;
    handled_reqs_num++;
    pthread_mutex_unlock(&buf_lock);
    return connfd;
}

//-----------------------------------------------MULTI THREADING----------------------------------------//

void* worker_routine(void* args)
{
    int connfd;
    while (1){
        connfd = pop_buffer(max_queue_size);
        requestHandle(connfd);
        Close(connfd);
        pthread_mutex_lock(&buf_lock);
        if (queue_is_full())
        {
            pthread_cond_signal(&buf_cond); // clearing room in queue. wake up master.
        }
        handled_reqs_num--;
        pthread_mutex_unlock(&buf_lock);
    }
}

int create_worker_threads(int num_threads)
{
    pthread_t *threads = malloc((num_threads+SAFETY_MARGIN)*sizeof(pthread_t));
    for (size_t i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_routine, NULL);
    }
    return 0;
}


//-----------------------------------------------MAIN----------------------------------------//

int main(int argc, char *argv[])
{
    int port, num_threads, max_q_size, listenfd, connfd, clientlen;
    char *sched_alg;
    struct sockaddr_in clientaddr;
    void* sched_func;
    
    getargs(&port, &num_threads, &max_q_size, &sched_alg, argc, argv);
    init_global_vars(max_q_size);
    init_buf_lock();
    create_worker_threads(num_threads);
    sched_func = parse_sched_alg(sched_alg);

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen); //should stay in main thread
    push_buffer(connfd, sched_func);
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


    


 
