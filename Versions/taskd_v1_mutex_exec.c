#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>

#define MAX_TASKS 100
#define NUM_WORKERS 5
#define LOGFILE "taskd.log"

pthread_mutex_t queue_lock, e_queue_lock;
pthread_cond_t not_empty, not_full, e_not_empty, e_not_full;

int ext = 0;

typedef struct{
	int id;
	char name[20];	
}Task;

typedef struct{
	int id;
	char *args[5];
}Exec;

Task task_queue[MAX_TASKS];		//for regular tasks
Exec exec_queue[MAX_TASKS];		//for execute tasks
int front = 0, rear = 0, count = 0;
int e_front = 0, e_rear = 0, e_count = 0;
int task_id = 0;

void log_task(char* msg)
{
	int fd = open(LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if(fd == -1)
		return;

	char buf[2048];
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

void exec_enqueue(Exec e)
{
	exec_queue[e_rear] = e;
	rear = (e_rear + 1)%MAX_TASKS;
	e_count++;
}

Task dequeue()
{
	Task t = task_queue[front];
	front = (front + 1)%MAX_TASKS;
	count--;
	return t;
}

Exec exec_dequeue()
{
	Exec e = exec_queue[e_front];
	front = (e_front+1)%MAX_TASKS;
	e_count--;
	return e;
}


void* exec_task(void* args)
{
	int piped[2];
	char buffer[1000];
	char log_buf[2048];
        while(!ext)
        {
		if(pipe(piped)==-1)
                {
                        perror("pipe");
                        exit(EXIT_FAILURE);
                }

               	pthread_mutex_lock(&e_queue_lock);

                while((e_count == 0) && !ext)
	                pthread_cond_wait(&e_not_empty, &e_queue_lock);
        
		if(ext)
			break;


		Exec curr = exec_dequeue();

		pid_t pid = fork();
		if(pid == 0)
		{
			close(piped[0]);

			fflush(stdout);

			dup2(piped[1], STDOUT_FILENO);

			close(piped[1]);

			execvp(curr.args[1], &(curr.args[1]));

			perror("execvp failed");
			exit(1);
		}
		else
		{
			close(piped[1]);
			int n = read(piped[0], buffer, sizeof(buffer));
		
			buffer[n] = '\0';

			char name[25];
		
			snprintf(name, sizeof(name), "%s %s", curr.args[0], curr.args[1]);

			snprintf(log_buf, sizeof(log_buf), "%s : %s", name, buffer); 

			//printf("%s\n", log_buf);

			log_task(log_buf);

			wait(NULL);
			close(piped[0]);

			pthread_cond_signal(&e_not_full);
               		pthread_mutex_unlock(&e_queue_lock);

			sleep(1 + rand() % 3);
		}
        }
	return NULL;
}


void* worker_task(void* args)
{
	int id = *(int*)args;

	while(!ext)
	{
		pthread_mutex_lock(&queue_lock);
		while((count == 0) && !ext)
			pthread_cond_wait(&not_empty, &queue_lock);

		if(ext)
		{
			pthread_mutex_unlock(&queue_lock);
			break;
		}

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
		{
			ext = 1;
			pthread_cond_broadcast(&not_full);
			pthread_cond_broadcast(&not_empty);
			pthread_cond_broadcast(&e_not_full);
			pthread_cond_broadcast(&e_not_empty);
			break;
		}

		if(strncmp(buf, "exec", 4) == 0)
		{
			pthread_mutex_lock(&e_queue_lock);

			while((e_count == MAX_TASKS) && !ext)
				pthread_cond_wait(&e_not_full, &e_queue_lock);

	                if(ext)
	                        break;

			Exec e;
			memset(&e, 0, sizeof(Exec));
			e.id = ++task_id;
			int i = 0;
			char* temp  = strtok(buf, " ");
		       while(temp != NULL && i < 4)
		       {
		       		e.args[i++] = temp;
				temp = strtok(NULL, " ");
		       }

		       e.args[i] = NULL;

		       exec_enqueue(e);
			
                        char log_buf[256];
                        snprintf(log_buf, sizeof(log_buf), "Added task %d: %s", e.id, e.args[0]);
                        log_task(log_buf);

			pthread_cond_signal(&e_not_empty);
			pthread_mutex_unlock(&e_queue_lock);
		}
		else
		{

			pthread_mutex_lock(&queue_lock);
			while((count == MAX_TASKS) && !ext)
				pthread_cond_wait(&not_full, &queue_lock);
	
	                if(ext)
	                        break;

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
	}
	return;
}

int main()
{
	int i = 0;
	srand(time(NULL));

	pthread_t worker[NUM_WORKERS];
	pthread_t executer;
	int ids[NUM_WORKERS];

	pthread_mutex_init(&queue_lock, NULL);
	pthread_cond_init(&not_empty, NULL);
	pthread_cond_init(&not_full, NULL);

	for(i = 0; i < NUM_WORKERS; i++)
	{
		ids[i] = i+1;
		pthread_create(&worker[i], NULL, worker_task, &ids[i]);
	}
	
	pthread_create(&executer, NULL, exec_task, &i);

	accept_jobs();

	for(i = 0; i < NUM_WORKERS; i++)
	{
		pthread_join(worker[i], NULL);
	}

	pthread_join(executer, NULL);

	printf("Shutting down.....\n");
	return 0;
}

