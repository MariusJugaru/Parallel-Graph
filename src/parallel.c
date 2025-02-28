// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
/* TODO: Define graph synchronization mechanisms. */
pthread_mutex_t visitMutex;
sem_t semaphoreVisit;

/* TODO: Define graph task argument. */
void process_neighbours(void *arg)
{
	os_node_t *node_arg;
	os_task_t *task;
	unsigned int arg_id;
	int *node_id;

	arg_id = *(unsigned int *)arg;
	node_arg = graph->nodes[arg_id];

	//printf("IN\n");
	sem_wait(&semaphoreVisit);
	for (unsigned int i = 0; i < node_arg->num_neighbours; i++) {
		if (graph->visited[node_arg->neighbours[i]] == NOT_VISITED) {
			node_id = malloc(sizeof(int *));
			*node_id = node_arg->neighbours[i];
			// node = malloc(sizeof(*node));
			graph->visited[*node_id] = DONE;
			sum += graph->nodes[*node_id]->info;
			// printf("SUM: %d\n", sum);

			task = create_task(&process_neighbours, node_id, &free);
			enqueue_task(tp, task);
		}
	}
	sem_post(&semaphoreVisit);
	//printf("OUT\n");
}

static void process_node(unsigned int idx)
{
	/* TODO: Implement thread-pool based processing of graph. */
	os_task_t *task;
	int *node_id;

	node_id = malloc(sizeof(int *));
	*node_id = idx;

	sum += graph->nodes[idx]->info;
	graph->visited[idx] = DONE;

	task = create_task(&process_neighbours, node_id, &free);
	enqueue_task(tp, task);
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	/* TODO: Initialize graph synchronization mechanisms. */
	sem_init(&semaphoreVisit, 0, 1);

	tp = create_threadpool(NUM_THREADS);
	process_node(0);
	wait_for_completion(tp);
	destroy_threadpool(tp);

	printf("%d", sum);

	sem_destroy(&semaphoreVisit);
	return 0;
}
