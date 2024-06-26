#include "segel.h"
#include "request.h"
#define ARG_MAX_LEN 10
#define SAFETY_MARGIN 10
#define NULL_REQUEST {-1,0,0}
#define END_OF_BUFFER {-2,0,0}
#define BLOCK "block"
#define DROP_TAIL "dt"
#define DROP_HEAD "dh"
#define BLOCK_FLUSH "bf"
#define DROP_RANDOM "random"

struct Req{
    int fd;
    struct timeval arrival_time; 
    struct timeval dispatch_time;
};

struct Req *requests_buffer;
int buf_end, buf_start, queue_size, max_queue_size, handled_reqs_num;
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

void print_buffer_stats(int pop){
    if (pop)
    {
        fprintf(stderr, "------------POP buffer. printing buffer:---------------\n buf_start: %d\n",buf_start);
    }
    else
    {
        fprintf(stderr, "------------PUSH buffer. printing buffer:---------------\n buf_start: %d\n",buf_start);
    }
    for (int i = 0; i < queue_size; i++)
    {
        fprintf(stderr, "buffer entry %u:\n address: %u\n",i, (unsigned int)(requests_buffer + buf_start + i));
    }
    fprintf(stderr, "buf_end: %u, buffer end address: %u\n",buf_end ,(unsigned int)(requests_buffer + buf_end));
}

//--------------------------------------------INIT----------------------------------------//

void getargs(int *port, int *num_threads, int *max_q_size, char *sched_alg[], int argc, char *argv[])
{
    if (argc < 4) {
	// fprintf(stderr, "Usage: %s <port>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *num_threads = atoi(argv[2]);
    *max_q_size = atoi(argv[3]);
    *sched_alg = argv[4];
}

void init_global_vars(int max_q_size)
{
    buf_start = 0;
    buf_end = 0;
    queue_size = 0;
    handled_reqs_num = 0;
    max_queue_size = max_q_size;
    requests_buffer = malloc((sizeof(struct Req))*(max_q_size + SAFETY_MARGIN));
}

void init_buf_lock()
{
    pthread_mutex_init(&buf_lock, NULL);
    pthread_cond_init(&buf_cond, NULL);
    pthread_cond_init(&master_cond, NULL);
}


//-----------------------------------------------BUFFER ACTIONS----------------------------------------//

void push_buffer(struct Req request, void (*sched_func)(struct Req))
{
    // fprintf(stderr, "push buffer. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);


    // fprintf(stderr, "push buffer. entered lock \n");
    if (queue_is_full())  // only add request if there is room in queue
    {
        sched_func(request);
        return;
    }
    requests_buffer[buf_end] = request; // push to cyclic queue
    buf_end = (buf_end + 1) % max_queue_size; // cyclic queue since we are removing the oldest request every time
    queue_size++;
        
        // print_buffer_stats(0);

    pthread_cond_signal(&buf_cond);
    pthread_mutex_unlock(&buf_lock);    
}

struct Req pop_buffer()
{
    // fprintf(stderr, "worker. pop buffer. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
    struct Req req;
    struct timeval dispatch;

    gettimeofday(&dispatch, NULL);
    req = requests_buffer[buf_start]; // remove from start of cyclic queue
    buf_start = (buf_start + 1) % max_queue_size; 
    queue_size--;
    handled_reqs_num++;
    
    timersub(&dispatch, &req.arrival_time, &req.dispatch_time);

    // print_buffer_stats(1);

    return req;
}

void pop_index(int index)
{
    //called when locked
    struct Req req_to_remove = requests_buffer[index];
    for (int i = index; i > buf_start; i--)
    {
        requests_buffer[i] = requests_buffer[i-1]; 
    }    
    requests_buffer[buf_start] = req_to_remove;
    
    req_to_remove = pop_buffer();
    handled_reqs_num--;
    Close(req_to_remove.fd);
}

//-----------------------------------------------MULTI THREADING----------------------------------------//

void* worker_routine(void* args)
{
    int tid = *(int*)args;
    free(args);
    struct Threads_stats tstats = {tid, 0, 0, 0};
    struct Req req;
    while (1){
        pthread_mutex_lock(&buf_lock);
        while (queue_size == 0)
        {
            // fprintf(stderr, "worker. queue size == 0. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
            pthread_cond_wait(&buf_cond, &buf_lock);
        }
        req = pop_buffer();
        pthread_mutex_unlock(&buf_lock);
        // fprintf(stderr, "worker. popped. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        
        requestHandle(req.fd, req.arrival_time, req.dispatch_time, &tstats);

        // fprintf(stderr, "worker. handled. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        Close(req.fd);
        pthread_mutex_lock(&buf_lock);
        if (queue_is_full())
        {
            // fprintf(stderr, "worker. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
            handled_reqs_num--;
            pthread_cond_signal(&master_cond); // clearing room in queue. wake up master.
        }
        else
        {
            handled_reqs_num--;
        }
        if (handled_reqs_num == 0)
        {
            pthread_cond_signal(&master_cond); // wake up master in case overload policy is block and wait.
        }
        
        // fprintf(stderr, "worker. unlocking. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        pthread_mutex_unlock(&buf_lock);
    }
}

int create_worker_threads(int num_threads)
{
    pthread_t *threads = malloc((num_threads+SAFETY_MARGIN)*sizeof(pthread_t));
    int* tid;
    for (size_t i = 0; i < num_threads; i++)
    {
        tid = malloc(sizeof(int));
        *tid = i;
        pthread_create(&threads[i], NULL, worker_routine, tid);
    }
    return 0;
}

//--------------------------------------------SCHEDULING ALGORITHMS-------------------------------//

void master_block_and_wait(struct Req req);
void drop_tail(struct Req req);
void drop_head(struct Req req);
void block_flush(struct Req req);
void drop_random(struct Req req);
void* parse_sched_alg(char* sched_alg_string)
{
   if (!strcmp(sched_alg_string, BLOCK))
   {
    	// fprintf(stderr, "policy: BLOCK");
        return master_block_and_wait;
   }
   else if (!strcmp(sched_alg_string, DROP_TAIL))
   {
        // fprintf(stderr, "policy: DROP TAIL");
        return drop_tail;
   }
   else if (!strcmp(sched_alg_string, DROP_HEAD))
   {
    // fprintf(stderr, "policy: DROP HEAD");
        return drop_head;
   }
   else if (!strcmp(sched_alg_string, BLOCK_FLUSH))
   {
    // fprintf(stderr, "policy: BLOCK FLUSH");
        return block_flush;
   }
   else if (!strcmp(sched_alg_string, DROP_RANDOM))
   {
    // fprintf(stderr, "policy: DROP RANDOM");
        return drop_head;
   }
   // fprintf(stderr, "policy: %s", sched_alg_string);
   return master_block_and_wait;
}

//------------------all of these functions are called while holding mutex.------------------//

void master_block_and_wait(struct Req req)
{
    while (queue_is_full())
    {
        // fprintf(stderr, "master block. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        pthread_cond_wait(&master_cond, &buf_lock);  // the master thread must block and wait if the queue is full
    }
    push_buffer(req, master_block_and_wait);    
    pthread_mutex_unlock(&buf_lock);
}

void drop_tail(struct Req req)
{
    Close(req.fd);
    pthread_mutex_unlock(&buf_lock);
}

void drop_head(struct Req req)
{
    struct Req old_req = pop_buffer();
    handled_reqs_num--; // needed because the popping increments it by 1. should be done while locked.
    Close(old_req.fd);
    push_buffer(req, drop_head);
}

void block_flush(struct Req req)
{
    while (handled_reqs_num + queue_size > 0)
    {
        // fprintf(stderr, "master block until queue is empty. queue size: %d, handled requests: %d,\n", queue_size,handled_reqs_num);
        pthread_cond_wait(&master_cond, &buf_lock);  // the master thread must block and wait until the queue is empty
    }
    Close(req.fd);
    pthread_mutex_unlock(&buf_lock);
}

void drop_random(struct Req req)
{
    srand(time(NULL));
    int half_size = (handled_reqs_num % 2 == 0) ? handled_reqs_num/2 : handled_reqs_num/2 + 1;
    int curr_size = handled_reqs_num;
    while (curr_size > half_size)
    {
        pop_index(rand() % curr_size);
        curr_size--;
    }
    push_buffer(req, drop_random);
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
    struct timeval arrival;
    gettimeofday(&arrival, NULL);
    struct Req req = {connfd, arrival, arrival};
    pthread_mutex_lock(&buf_lock);
    push_buffer(req, sched_func);
    }
    free (requests_buffer);
}


    


 
