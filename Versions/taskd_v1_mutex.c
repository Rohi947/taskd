#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fctnl.h>
#include <time.h>

#define MAX_TASKS 100
#define NUM_WORKERS 3
#define LOGFILE "taskd.log"

typedef struct{
	int id;
	char name[30];
}Task;

Task task_queue[MAX_TASKS];
int front = 0, rear = 0, count = 0;
int task_id = 0;

pthread_mutex_t queue_lock;
pthread_cond_t not_empty, not_full;

void log_task(const char *msg);
void enqueue(Task t);
Task dequeue(void);
void* worker_thread(void* args);
void accept_jobs(void);

int main()
{
	srand(time(NULL));
	pthread_t workers[NUM_WORKERS];
	int ids[NUM_WORKERS];

	pthread_mutex_init(&queue_lock, NULL);
	pthread_cond_init(&not_empty, NULL);
	pthread_cond_init(&not_full, NULL);

	for(int i = 0; i < NUM_WORKERS; i++)
	{
		ids[i] = i+1;
		pthread_create(&workers[i], NULL, worker_thread, &ids[i]);
	}

	accept_jobs();
	printf("Shutting down...\n");
	return 0;

}
