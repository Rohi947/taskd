#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define MAX_TASKS 100
#define NUM_WORKERS 5
#define LOGFILE "taskd.log"

pthread_mutex_t queue_lock;
pthread_cond_t not_empty, not_full;

typedef struct{
	int id;
	char name[20];	
}Task;

Task task_queue[MAX_TASKS];
int front = 0, rear = 0, count = 0;
int task_id = 0;

void log_task(char* msg)
{
	int fd = open(LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if(fd == -1)
		return;

	char buf[256];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	int len = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] %s\n", t->tm_hour, t->tm_min, t->tm_sec, msg);

	write(fd, buf, len);
	close(fd);
}

void enqueue(Task t)
{
	task_queue[rear] = t;
	rear = (rear + 1)%MAX_TASKS;
	count++;
}

Task dequeue(void)
{
	Task t = task_queue[front];
	front = (front + 1)%MAX_TASKS;
	count--;
	return t;
}

void* worker_task(void* args)
{
	int id = *(int*)args;

	while(1)
	{
		pthread_mutex_lock(&queue_lock);
		while(count == 0)
			pthread_cond_wait(&not_empty, &queue_lock);

		char buf[256];
		Task t = dequeue();
		snprintf(buf, sizeof(buf), "Worker %d processing task %d : %s", id, t.id, t.name);
		log_task(buf);

		pthread_cond_signal(&not_full);
		pthread_mutex_unlock(&queue_lock);
	
		sleep(1 + rand() % 3);
	}
	return NULL;
}

void accept_jobs(void)
{
	char buf[50];
	while(1)
	{
		printf("taskd> ");
		if(!fgets(buf, sizeof(buf), stdin))break;
		buf[strcspn(buf, "\n")] = 0;

		if(strcmp(buf, "exit") == 0)
			break;

		pthread_mutex_lock(&queue_lock);
		while(count == MAX_TASKS)
			pthread_cond_wait(&not_full, &queue_lock);

		Task t;
		t.id = ++task_id;
		strncpy(t.name, buf, sizeof(t.name));
		enqueue(t);

		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Added task %d: %s", t.id, t.name);
		log_task(log_buf);

		pthread_cond_signal(&not_empty);
		pthread_mutex_unlock(&queue_lock);
	}
	return;
}

int main()
{
	srand(time(NULL));

	pthread_t worker[NUM_WORKERS];
	int ids[NUM_WORKERS];

	pthread_mutex_init(&queue_lock, NULL);
	pthread_cond_init(&not_empty, NULL);
	pthread_cond_init(&not_full, NULL);

	for(int i = 0; i < NUM_WORKERS; i++)
	{
		ids[i] = i+1;
		pthread_create(&worker[i], NULL, worker_task, &ids[i]);
	}
	
	accept_jobs();
	printf("Shutting down.....\n");
	return 0;
}

