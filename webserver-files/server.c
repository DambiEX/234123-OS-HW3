#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST -1
#define END_OF_BUFFER -2
#define BLOCK "block"
#define DROP_TAIL "drop_tail"
#define DROP_HEAD "drop_head"
int *requests_buffer, buf_end, buf_start, queue_size, max_queue_size, handled_reqs_num;
pthread_mutex_t buf_lock;
pthread_cond_t buf_cond, master_cond;


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
    return ((queue_size + handled_reqs_num) >= max_queue_size);
}

//--------------------------------------------INIT----------------------------------------//

void getargs(int *port, int *num_threads, int *max_q_size, char *sched_alg[], int argc, char *argv[])
{
    if (argc < 4) {
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
    max_queue_size = max_q_size;
    requests_buffer = malloc(max_q_size + SAFETY_MARGIN);
    initialize_buffer(max_q_size, requests_buffer);
}

void init_buf_lock()
{
    pthread_mutex_init(&buf_lock, NULL);
    pthread_cond_init(&buf_cond, NULL);
    pthread_cond_init(&master_cond, NULL);
}


//-----------------------------------------------BUFFER ACTIONS----------------------------------------//

void push_buffer(int connfd, void (*sched_func)(int connfd))
{
    fprintf(stderr, "push buffer. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
    pthread_mutex_lock(&buf_lock);
    fprintf(stderr, "push buffer. entered lock \n");
    if (queue_is_full())  // only add request if there is room in queue
    {
        sched_func(connfd);
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
    fprintf(stderr, "worker. pop buffer. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
    int connfd;
    
    pthread_mutex_lock(&buf_lock);
    while (queue_size == 0)
    {
        fprintf(stderr, "worker. queue size == 0. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
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
        fprintf(stderr, "worker. popped. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        requestHandle(connfd);
        fprintf(stderr, "worker. handled. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        Close(connfd);
        pthread_mutex_lock(&buf_lock);
        if (queue_is_full())
        {
            fprintf(stderr, "worker. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
            handled_reqs_num--;
            pthread_cond_signal(&master_cond); // clearing room in queue. wake up master.
        }
        else
        {
            handled_reqs_num--;
        }
        fprintf(stderr, "worker. unlocking. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
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

//--------------------------------------------SCHEDULING ALGORITHMS-------------------------------//

void master_block_and_wait();
void drop_tail(int connfd);
void drop_head(int new_connfd);
void* parse_sched_alg(char* sched_alg_string)
{
   if (!strcmp(sched_alg_string, BLOCK))
   {
    	fprintf(stderr, "policy: BLOCK");
        return master_block_and_wait;
   }
   else if (!strcmp(sched_alg_string, DROP_TAIL))
   {
        fprintf(stderr, "policy: DROP TAIL");
        return drop_tail;
   }
   else if (!strcmp(sched_alg_string, DROP_HEAD))
   {
    fprintf(stderr, "policy: DROP HEAD");
        return drop_head;
   }
   fprintf(stderr, "policy: %s", sched_alg_string);
   return master_block_and_wait;
}

//------------------all of these functions are called while holding mutex.------------------//

void master_block_and_wait(int connfd)
{
    while (queue_is_full())
    {
        fprintf(stderr, "master block. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        pthread_cond_wait(&master_cond, &buf_lock);  // the master thread must block and wait if the queue is full
    }
    pthread_mutex_unlock(&buf_lock);
    push_buffer(connfd, master_block_and_wait);
}

void drop_tail(int connfd)
{
    Close(connfd);
    pthread_mutex_unlock(&buf_lock);
}

void drop_head(int new_connfd)
{
    handled_reqs_num--; // needed because the popping increments it by 1. should be done while locked.
    pthread_mutex_unlock(&buf_lock); // note: unlock of unlocked mutex is undefined!
    int old_connfd = pop_buffer(max_queue_size);
    Close(old_connfd);
    push_buffer(new_connfd, drop_head);
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
    }
    free (requests_buffer);
}


    


 
