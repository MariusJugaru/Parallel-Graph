// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

static unsigned int free_threads;
static unsigned int wait_first_task;
static unsigned int check_new_task;

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	/* TODO: Enqueue task to the shared task queue. Use synchronization. */
	sem_wait(&(tp->semaphoreEnqueue));
	list_add(&(tp->head), &(t->list));
	// First task ready
	if (!wait_first_task) {
		wait_first_task = 1;
		sem_post(&(tp->waitFirstTask));
	}
	sem_post(&(tp->semaphoreEnqueue));
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t;

	/* TODO: Dequeue task from the shared task queue. Use synchronization. */
	// Wait first task
	if (!wait_first_task) {
		sem_wait(&(tp->waitFirstTask));
		sem_post(&(tp->waitFirstTask));
	}

	while (1) {
		pthread_mutex_lock(&(tp->mutex2));
		if (!(queue_is_empty(tp))) {
			sem_wait(&(tp->semaphoreEnqueue));
			t = list_entry(tp->head.prev, os_task_t, list);
			list_del(tp->head.prev);
			sem_post(&(tp->semaphoreEnqueue));

			// A task might've become available
			if (!check_new_task) {
				check_new_task = 1;
				sem_post(&(tp->taskAvailable));
			}
			pthread_mutex_unlock(&(tp->mutex2));
			return t;
		}
		pthread_mutex_unlock(&(tp->mutex2));

		pthread_mutex_lock(&(tp->mutex));
		free_threads++;
		if (free_threads == tp->num_threads) {
			// printf("IM FREE\n");
			pthread_mutex_unlock(&(tp->mutex));
			sem_post(&(tp->taskAvailable));
			sem_post(&(tp->waitForWorkers));
			return NULL;
		}
		pthread_mutex_unlock(&(tp->mutex));

		if (queue_is_empty(tp)) {
			// Block until a task becomes available
			//printf("BLOCKED\n");
			sem_wait(&(tp->taskAvailable));
			check_new_task = 0;
			//printf("UNBLOCKED\n");
		}

		//printf("FREE THREADS: %d\n", free_threads);
		pthread_mutex_lock(&(tp->mutex));
		free_threads--;
		pthread_mutex_unlock(&(tp->mutex));
	}
	return NULL;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* TODO: Wait for all worker threads. Use synchronization. */
	sem_wait(&(tp->waitForWorkers));
	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	free_threads = 0;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* TODO: Initialize synchronization data. */
	pthread_mutex_init(&(tp->mutex), NULL);
	pthread_mutex_init(&(tp->mutex2), NULL);

	sem_init(&(tp->waitFirstTask), 0, 0);
	sem_init(&(tp->taskAvailable), 0, 0);
	sem_init(&(tp->waitForWorkers), 0, 0);
	sem_init(&(tp->semaphoreEnqueue), 0, 1);

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	/* TODO: Cleanup synchronization data. */
	pthread_mutex_destroy(&(tp->mutex));
	pthread_mutex_destroy(&(tp->mutex2));

	sem_destroy(&(tp->waitFirstTask));
	sem_destroy(&(tp->taskAvailable));
	sem_destroy(&(tp->waitForWorkers));
	sem_destroy(&(tp->semaphoreEnqueue));

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
