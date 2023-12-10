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
#define STARTING_NODE	0

static _Atomic int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;

// Mutex used to read and write to visited array of the graph
pthread_mutex_t visited_mutex;

static void *get_uint(unsigned int integer);
static void destory_uint(void *heap_uint);
static void parallel_process_node(void *heap_uint);

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

	// Synchronization mechanisms initialization
	pthread_mutex_init(&visited_mutex, NULL);
	atomic_store(&sum, 0);

	tp = create_threadpool(NUM_THREADS);

	// Create the first task
	void *starting_node = get_uint(STARTING_NODE);

	graph->visited[STARTING_NODE] = PROCESSING;
	os_task_t *first_task = create_task(&parallel_process_node, starting_node, &destory_uint, 0);

	enqueue_task(tp, first_task);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	// Synchronization mechanisms destruction
	pthread_mutex_destroy(&visited_mutex);

	printf("%d", sum);

	return 0;
}

static void *get_uint(unsigned int integer)
{
	unsigned int *heap_uint = malloc(sizeof(unsigned int));

	DIE(!heap_uint, "malloc failed\n");

	*heap_uint = integer;

	return (void *)heap_uint;
}

static void destory_uint(void *heap_uint)
{
	free(heap_uint);
}

static void parallel_process_node(void *heap_uint)
{
	unsigned int idx = *(unsigned int *)heap_uint;

	atomic_fetch_add(&sum, graph->nodes[idx]->info);

	// Go through the neighbours, and if they aren't visited, create new tasks
	// for them
	for (unsigned int i = 0; i < graph->nodes[idx]->num_neighbours; i++) {
		unsigned int arg = graph->nodes[idx]->neighbours[i];

		pthread_mutex_lock(&tp->list_mutex);
		if (graph->visited[arg] != NOT_VISITED) {
			pthread_mutex_unlock(&tp->list_mutex);
			continue;
		}
		graph->visited[arg] = PROCESSING;
		pthread_mutex_unlock(&tp->list_mutex);

		void *heap_idx = get_uint(arg);

		os_task_t *new_task = create_task(&parallel_process_node, heap_idx, &destory_uint, arg);

		enqueue_task(tp, new_task);
	}


	pthread_mutex_lock(&tp->list_mutex);
	graph->visited[idx] = DONE;
	pthread_mutex_unlock(&tp->list_mutex);
}


